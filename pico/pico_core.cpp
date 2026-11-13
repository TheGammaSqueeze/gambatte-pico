// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 TheGammaSqueeze
/*
 * pico_core.cpp - the blob side of the LK's GB/GBC core interface (gbc_core_abi.h).
 *
 * Publishes the export table the LK drives the core through, keeps the imports table the LK
 * passes at init, and provides the few outward calls the core needs (input, time) plus an
 * atexit stub. libc mem* (pico_libc.c), libgcc __aeabi_*, libm powf and the C++ runtime
 * (pico_runtime.cpp) are linked into the blob separately.
 */
#include "gbc_core_abi.h"

/* The authentic gambatte GB-colorization palette catalogue (GB/GBC/SGB/Special +
 * the TWB64/community packs). Compiled into the blob so the frontend does not have
 * to duplicate ~600 palettes; it browses/applies by index over the ABI below.
 *
 * gbcpalettes.h pulls in libretro-common's array/rhmap.h for a title->palette hash
 * lookup we do not use (index access only). rhmap.h drags in retro_common_api.h,
 * which #errors under our freestanding -ffreestanding toolchain ("inttypes.h is
 * being screwy"). We only need the palette TABLES, so short-circuit the rhmap include
 * by pre-defining its guard and stubbing the three macros the (unused, dropped) map
 * helpers reference so the header still parses. */
#define __LIBRETRO_SDK_ARRAY_RHMAP_H__ 1
#ifndef NULL
#define NULL 0
#endif
#define RHMAP_SET_STR(b, k, v) ((void)(b), (void)(k), (void)(v))
#define RHMAP_GET_STR(b, k)    ((void)(b), (void)(k), (const unsigned short *)0)
#define RHMAP_FREE(b)          ((void)(b))
#include "gbcpalettes.h"   /* upstream libgambatte/libretro/gbcpalettes.h */

/* extern "C" core wrappers (pico_bridge.cpp) */
extern "C" {
void     gbc_heap_init(void *base, unsigned size);
int      gbc_create(void);
int      gbc_load(const void *rom, unsigned size, unsigned flags);
void     gbc_reset(void);
long     gbc_run(unsigned short *video, int pitch, unsigned int *sound, unsigned sound_sz, unsigned *samples);
void    *gbc_savedata_ptr(void);
unsigned gbc_savedata_size(void);
void    *gbc_rtcdata_ptr(void);
unsigned gbc_rtcdata_size(void);
unsigned gbc_state_size(void);
void     gbc_save_state(void *buf);
int      gbc_load_state(const void *buf, unsigned size);
void     gbc_set_dmg_palette_color(unsigned pal, unsigned col, unsigned rgb32);
void     gbc_set_color_correction(int enable);
void     gbc_set_color_correction_mode(unsigned mode);
void     gbc_set_dark_filter(unsigned level);
int      gbc_has_accelerometer(void);
int      gbc_rumble(void);
void     gbc_set_accelerometer(unsigned x, unsigned y);
}

static const struct gbc_core_imports *s_imp;

/* ---- GB colorization palette catalogue over gbcDirPalettes[] (gbcpalettes.h) ----
 * Each entry is a name plus 12 PACK15 (15-bit BGR555) colours = 3 DMG palettes (BG,
 * OBJ0, OBJ1) x 4 shades. Convert BGR555 -> RGB565 (see pack15_to_rgb565) and drive the
 * existing gbc_set_dmg_palette_color path so nothing else in the core needs to change. */
static const unsigned GBC_DIR_PAL_N =
	(unsigned)(sizeof(gbcDirPalettes) / sizeof(gbcDirPalettes[0]));

/* gbcpalettes.h PACK15 is 15-bit BGR555 (B<<10 | G<<5 | R). This core is built with
 * -DVIDEO_RGB565, so gambatte's video_pixel_t is a 16-bit RGB565 value that
 * setDmgPaletteColor stores DIRECTLY as the output pixel (it does NOT run our value
 * through gbcToRgb32). So convert BGR555 -> RGB565 and hand THAT over; a 24-bit 0xRRGGBB
 * would be truncated to 16 bits = garbage (the "every game the same green/blue" bug). */
static inline unsigned pack15_to_rgb565(unsigned short v)
{
	unsigned r5 = v & 0x1Fu, g5 = (v >> 5) & 0x1Fu, b5 = (v >> 10) & 0x1Fu;
	unsigned g6 = (g5 << 1) | (g5 >> 4);
	return (r5 << 11) | (g6 << 5) | b5;
}

static int str_eq(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}

extern "C" unsigned gbc_dmg_palette_count(void) { return GBC_DIR_PAL_N; }

extern "C" const char *gbc_dmg_palette_name(unsigned idx)
{
	return idx < GBC_DIR_PAL_N ? gbcDirPalettes[idx].title : "";
}

extern "C" void gbc_dmg_palette_apply(unsigned idx)
{
	if (idx >= GBC_DIR_PAL_N) return;
	const unsigned short *p = gbcDirPalettes[idx].p;   /* 12 shorts: BG, OBJ0, OBJ1 */
	for (unsigned pal = 0; pal < 3; pal++)
		for (unsigned col = 0; col < 4; col++)
			gbc_set_dmg_palette_color(pal, col, pack15_to_rgb565(p[pal * 4 + col]));
}

extern "C" unsigned gbc_dmg_palette_default(void)
{
	for (unsigned i = 0; i < GBC_DIR_PAL_N; i++)
		if (str_eq(gbcDirPalettes[i].title, "GBC - Dark Green"))
			return i;                          /* the CGB BIOS default GBC palette */
	return 0;
}

/* gambatte "internal" colorization: match the ROM header title against the per-game
 * palette table (the palettes the CGB BIOS assigns by game). Returns the gbcDirPalettes
 * index when the fallback is used (so the frontend can name it), else -1 for a real
 * per-game hit. */
static const unsigned GBC_TITLE_PAL_N =
	(unsigned)(sizeof(gbcTitlePalettes) / sizeof(gbcTitlePalettes[0]));

static void apply_pal_ptr(const unsigned short *p)
{
	for (unsigned pal = 0; pal < 3; pal++)
		for (unsigned col = 0; col < 4; col++)
			gbc_set_dmg_palette_color(pal, col, pack15_to_rgb565(p[pal * 4 + col]));
}

extern "C" int gbc_dmg_palette_apply_auto(const char *title)
{
	if (title) {
		for (unsigned i = 0; i < GBC_TITLE_PAL_N; i++)
			if (str_eq(gbcTitlePalettes[i].title, title)) {
				apply_pal_ptr(gbcTitlePalettes[i].p);
				return -1;                 /* per-game hit */
			}
	}
	unsigned di = gbc_dmg_palette_default();    /* not listed -> default GBC palette */
	apply_pal_ptr(gbcDirPalettes[di].p);
	return (int)di;
}

/* ---- motion: the console's accelerometer as an MBC7 cartridge's sensor ----
 * The sensor sits flat in the cartridge, behind the screen, so it reads the console's
 * acceleration across the screen (X) and along it (Y), gravity included: 0x81d0 on an axis
 * that is level, about 0x70 for each g along it, X rising as the right side goes down and Y as
 * the top edge comes up (as Kirby Tilt 'n' Tumble rolls). The LK's readings are 2048 to a g. */
enum { MBC7_LEVEL = 0x81d0, MBC7_PER_G = 0x70, HOST_PER_G = 2048 };
static unsigned mbc7_axis(int v)
{
	long r = MBC7_LEVEL + (long)v * MBC7_PER_G / HOST_PER_G;
	return r < 0 ? 0 : r > 0xffff ? 0xffff : (unsigned)r;
}
extern "C" int gbc_motion_wanted(void) { return gbc_has_accelerometer(); }
extern "C" void gbc_set_motion(int x, int y, int z)
{
	(void)z;   /* the cartridge's sensor has no third axis */
	gbc_set_accelerometer(mbc7_axis(x), mbc7_axis(-y));
}

/* ---- outward-call forwarders the bundled core references ---- */
extern "C" unsigned gbc_read_buttons(void)      /* gambatte LkInput -> LK pad reader */
{
	return s_imp && s_imp->read_buttons ? s_imp->read_buttons() : 0;
}
extern "C" long time(long *t)                   /* cart RTC clock */
{
	long v = s_imp && s_imp->host_time ? s_imp->host_time() : 0;
	if (t) *t = v;
	return v;
}
extern "C" int atexit(void (*)(void)) { return 0; }   /* no teardown in the blob */

static const struct gbc_core_exports g_exports = {
	GBC_CORE_ABI_MAGIC,
	GBC_CORE_ABI_V2,
	gbc_heap_init,
	gbc_create,
	gbc_load,
	gbc_reset,
	gbc_run,
	gbc_savedata_ptr,
	gbc_savedata_size,
	gbc_rtcdata_ptr,
	gbc_rtcdata_size,
	gbc_state_size,
	gbc_save_state,
	gbc_load_state,
	gbc_set_dmg_palette_color,
	gbc_set_color_correction,
	gbc_set_color_correction_mode,
	gbc_set_dark_filter,
	gbc_dmg_palette_count,
	gbc_dmg_palette_name,
	gbc_dmg_palette_apply,
	gbc_dmg_palette_default,
	gbc_dmg_palette_apply_auto,
	gbc_motion_wanted,
	gbc_set_motion,
	gbc_rumble,
};

/* Blob entry: LK calls this with the imports table and gets the export table back. */
extern "C" const struct gbc_core_exports *gbc_core_blob_init(const struct gbc_core_imports *imp)
{
	s_imp = imp;
	return &g_exports;
}
