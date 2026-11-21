// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 TheGammaSqueeze
/*
 * pico_bridge.cpp - extern "C" bridge between the LK's C driver and the gambatte C++ core.
 * Only the AAPCS C ABI crosses this boundary.
 */
#include "pico_core_memory.h"
#include <gambatte.h>
#include <stddef.h>

#include <inputgetter.h>
#include <serial_io.h>

using namespace gambatte;

/* The machine every per-machine call below acts on. Normally the only one; with the link cable
 * (gbc_link_open) there is a second, and gbc_link_select picks which one these calls reach. */
static GB *g_gb;
static GB *s_mach[2];

extern "C" void gbc_heap_init(void *base, unsigned size);	/* pico_runtime.cpp */
extern "C" unsigned gbc_read_port(unsigned port);		/* pico_core.cpp */
extern "C" void *memcpy(void *, const void *, size_t);
extern "C" void *memset(void *, int, size_t);

/* Feeds a controller port's buttons into a machine: the first machine reads port 0 (the
 * console's own pad), the linked one port 1 (the other player, from the LK's netplay). */
namespace {
struct LkInput : InputGetter {
	unsigned port;
	unsigned operator()() { unsigned v = gbc_read_port(port); s_last_pad[port & 1] = v; return v; }
	static unsigned s_last_pad[2];
};
unsigned LkInput::s_last_pad[2];
}

/* Create the emulator (placement into the bump arena via operator new). */
extern "C" int gbc_create(void)
{
	LkInput *in;
	g_gb = new GB();
	if (!g_gb)
		return -1;
	in = new LkInput();
	if (!in)
		return -1;
	in->port = 0;
	g_gb->setInputGetter(in);
	s_mach[0] = g_gb;
	s_mach[1] = 0;
	return 0;
}

/* Load a ROM image from a memory buffer. flags=0 auto-detects CGB; pass
 * gambatte::GB::FORCE_DMG (1) to force original Game Boy (DMG) mode for .gb ROMs. */
extern "C" void gbc_rumble_clear(void);   /* pico_runtime.cpp */

extern "C" int gbc_load(const void *rom, unsigned size, unsigned flags)
{
	if (!g_gb)
		return -1;
	gbc_rumble_clear();   /* a new cartridge starts with its motor still */
	return g_gb->load(rom, size, flags);
}

extern "C" void gbc_reset(void)
{
	gbc_rumble_clear();
	if (g_gb)
		g_gb->reset();
}

/* ---- cartridge battery save (.sav) + RTC access ---- */
extern "C" void *gbc_savedata_ptr(void) { return g_gb ? g_gb->savedata_ptr() : 0; }
extern "C" unsigned gbc_savedata_size(void) { return g_gb ? g_gb->savedata_size() : 0; }
extern "C" void *gbc_rtcdata_ptr(void) { return g_gb ? g_gb->rtcdata_ptr() : 0; }
extern "C" unsigned gbc_rtcdata_size(void) { return g_gb ? g_gb->rtcdata_size() : 0; }

/* ---- the machine's memory for RetroAchievements (V4) ----
 * The Game Boy's address map: work RAM banks 0 and 1, high RAM, video RAM, the object table and the
 * registers after it, the cartridge's ROM banks in view and its RAM, and on a Color the work RAM banks
 * 2 to 7 where RetroAchievements places them (from 0x10000). */
extern "C" void gbc_get_memory(struct pico_core_memory *out)
{
	static struct pico_memdesc map[10];
	unsigned n = 0;
	memset(out, 0, sizeof *out);
	if (!g_gb) return;
	memset(map, 0, sizeof map);
#define GBC_MAP(fl, p, off, at, sel, size) do { map[n].flags = (fl); map[n].ptr = (p); map[n].offset = (off); \
	map[n].start = (at); map[n].select = (sel); map[n].len = (size); n++; } while (0)
	GBC_MAP(PICO_MEMDESC_SYSTEM_RAM, g_gb->rambank0_ptr(), 0, 0xC000, 0, 0x1000);
	GBC_MAP(PICO_MEMDESC_SYSTEM_RAM, g_gb->rambank1_ptr(), 0, 0xD000, 0, 0x1000);
	GBC_MAP(PICO_MEMDESC_SYSTEM_RAM, g_gb->zeropage_ptr(), 0, 0xFF80, 0, 0x0080);
	GBC_MAP(0, g_gb->vram_ptr(), 0, 0x8000, 0, 0x2000);
	GBC_MAP(0, g_gb->oamram_ptr(), 0, 0xFE00, 0xFFFFFFE0u, 0x00A0);
	GBC_MAP(0, g_gb->oamram_ptr(), 0x100, 0xFF00, 0, 0x0080);
	GBC_MAP(PICO_MEMDESC_CONST, g_gb->rombank0_ptr(), 0, 0x0000, 0, 0x4000);
	GBC_MAP(PICO_MEMDESC_CONST, g_gb->rombank1_ptr(), 0, 0x4000, 0, 0x4000);
	if (g_gb->savedata_size())
		GBC_MAP(0, g_gb->savedata_ptr(), 0, 0xA000, ~(size_t)0x1FFF, g_gb->savedata_size());
	if (g_gb->isCgb())
		GBC_MAP(PICO_MEMDESC_SYSTEM_RAM, g_gb->rambank2_ptr(), 0, 0x10000, 0xFFFFA000u, 0x6000);
#undef GBC_MAP
	out->system_ram = g_gb->rambank0_ptr();
	out->system_ram_size = g_gb->isCgb() ? 0x8000u : 0x2000u;
	out->save_ram = g_gb->savedata_ptr();
	out->save_ram_size = g_gb->savedata_size();
	out->descriptors = map;
	out->num_descriptors = n;
}

/* ---- MBC7 tilt cartridges: whether the cartridge has the sensor, and its reading ---- */
extern "C" int gbc_has_accelerometer(void) { return g_gb && g_gb->hasAccelerometer(); }
extern "C" void gbc_set_accelerometer(unsigned x, unsigned y)
{
	if (g_gb)
		g_gb->setAccelerometer(x, y);
}

/* ---- CGB colour / palette knobs (menu-driven) ---- */
extern "C" void gbc_set_color_correction(int enable)
{
	if (g_gb)
		g_gb->setColorCorrection(enable != 0);
}
extern "C" void gbc_set_color_correction_mode(unsigned mode)
{
	if (g_gb)
		g_gb->setColorCorrectionMode(mode);
}
extern "C" void gbc_set_dark_filter(unsigned level)
{
	if (g_gb)
		g_gb->setDarkFilterLevel(level);
}
/* DMG (mono) games only: set one of the 4 shades of a palette. No effect on a
 * CGB game, which supplies its own palettes. */
extern "C" void gbc_set_dmg_palette_color(unsigned palNum, unsigned colorNum,
					  unsigned rgb32)
{
	if (g_gb)
		g_gb->setDmgPaletteColor(palNum, colorNum, rgb32);
}

/* ---- save states (buffer based) ---- */
extern "C" unsigned gbc_state_size(void)
{
	return g_gb ? (unsigned)g_gb->stateSize() : 0;
}
extern "C" void gbc_save_state(void *buf)
{
	if (g_gb)
		g_gb->saveState(buf);
}
extern "C" int gbc_load_state(const void *buf, unsigned size)
{
	gbc_rumble_clear();   /* states do not carry the motor bit; the game sets it again */
	return (g_gb && g_gb->loadState(buf, size)) ? 0 : -1;
}

static long link_run(video_pixel_t *video, int pitch, uint_least32_t *sound, unsigned cap,
		     unsigned *samples);

/*
 * Run emulation. videoBuf is 160x144 RGB565 (pitch in pixels); soundBuf holds
 * up to soundBufSize stereo samples at 2097152 Hz (each u32 = L|R<<16). On
 * input *samples is the number of samples of time to run; on output it is the
 * count actually produced. Returns >=0 when a video frame was completed this
 * call (videoBuf is then valid), <0 otherwise.
 */
extern "C" long gbc_run(unsigned short *videoBuf, int pitch,
			unsigned int *soundBuf, unsigned soundBufSize,
			unsigned *samples)
{
	if (!g_gb)
		return -1;
	if (s_mach[1])
		return link_run((video_pixel_t *)videoBuf, pitch, (uint_least32_t *)soundBuf,
				soundBufSize, samples);
	return g_gb->runFor((video_pixel_t *)videoBuf, pitch,
			    (uint_least32_t *)soundBuf, soundBufSize, *samples);
}

/* ---- the link cable: two machines, wired to each other, in one core ----
 *
 * For two player games over the Bluetooth link both consoles run BOTH Game Boys, joined by an
 * emulated cable, and only the buttons travel between the consoles (the LK's lockstep netplay
 * gives each machine its player's pad every frame). Each console shows and plays one of them
 * (gbc_link_view), but steps both the same way, so the two consoles stay identical: whose
 * picture a console wants never changes how the machines are run.
 *
 * Stepping: the first machine runs a short slice (LINK_SLICE samples, a quarter of a
 * millisecond), then the second runs until it has done as much; the first machine's frame ends
 * the call. A byte clocked out by one machine reaches the other at most a slice late in its own
 * time, far inside what link protocols wait for.
 *
 * The cable: when a machine starts a transfer on its own clock, it gets the other machine's
 * serial byte at once and its own byte waits for the other side, which takes it the next time it
 * stands ready on the external clock (the transfer then ends there as it would on a cable). */
#define LINK_SLICE	512u		/* samples (2 MHz units) per slice */
#define LINK_W		160
#define LINK_H		144

namespace {
struct LinkEnd : SerialIO {
	GB *self;
	LinkEnd *other;
	bool waiting, fast;		/* a byte from the other machine waits here */
	unsigned char byte;
	unsigned bytes;			/* bytes this machine clocked out (gbc_link_stat) */

	bool check(unsigned char out, unsigned char &in, bool &fastCgb)
	{
		(void)out;
		if (!waiting)
			return false;
		in = byte;
		fastCgb = fast;
		waiting = false;
		return true;
	}
	unsigned char send(unsigned char data, bool fastCgb)
	{
		const unsigned char *io = (const unsigned char *)other->self->oamram_ptr();
		other->byte = data;
		other->fast = fastCgb;
		other->waiting = true;
		bytes++;
		return io[0x101];		/* the other machine's serial data register */
	}
};
}

static LinkEnd *s_end[2];
static video_pixel_t *s_draw[2], *s_shown[2];	/* being drawn, last whole frame */
static uint_least32_t *s_spare;			/* the unwatched machine's sound */
static unsigned s_spare_cap;
static unsigned s_view;				/* whose picture and sound gbc_run hands out */
static unsigned long long s_done[2];		/* samples each machine has run */
/* The cartridge clocks: each machine keeps its own console's time (a Game Boy Color clock in
 * Pokemon Crystal counts on from what its owner's console said), so the clock the LK hands out
 * (the session's) is moved by this machine's offset while it runs. */
static long s_clock_off[2];
static int s_running = -1;			/* the machine inside runFor, -1 none */

extern "C" void gbc_link_clock(unsigned m, long offset) { s_clock_off[m ? 1 : 0] = offset; }
extern "C" long gbc_link_clock_adjust(void) { return s_running >= 0 ? s_clock_off[s_running] : 0; }

/* The second machine, with this cartridge, wired to the first. 0 linked, -1 no memory, else the
 * load's error. Once per session: the arena gives nothing back. */
extern "C" int gbc_link_open(const void *rom, unsigned size, unsigned flags)
{
	LkInput *in;
	int i, rc;
	if (!s_mach[0] || s_mach[1])
		return -1;
	s_mach[1] = new GB();
	in = new LkInput();
	for (i = 0; i < 2; i++) {
		s_end[i] = new LinkEnd();
		s_draw[i] = new video_pixel_t[LINK_W * LINK_H];
		s_shown[i] = new video_pixel_t[LINK_W * LINK_H];
	}
	s_spare_cap = 4u * LINK_SLICE + 4096u;
	s_spare = new uint_least32_t[s_spare_cap];
	if (!s_mach[1] || !in || !s_end[0] || !s_end[1] || !s_draw[0] || !s_draw[1] || !s_shown[0] ||
	    !s_shown[1] || !s_spare) {
		s_mach[1] = 0;
		return -1;
	}
	in->port = 1;
	s_mach[1]->setInputGetter(in);
	rc = s_mach[1]->load(rom, size, flags);
	if (rc) {
		s_mach[1] = 0;
		return rc;
	}
	for (i = 0; i < 2; i++) {
		s_end[i]->self = s_mach[i];
		s_end[i]->other = s_end[i ^ 1];
		s_end[i]->waiting = false;
		s_end[i]->bytes = 0;
		s_mach[i]->setSerialIO(s_end[i]);
		for (unsigned k = 0; k < LINK_W * LINK_H; k++)
			s_draw[i][k] = s_shown[i][k] = 0;
		s_done[i] = 0;
	}
	s_view = 0;
	s_clock_off[0] = s_clock_off[1] = 0;
	return 0;
}

extern "C" int gbc_link_on(void) { return s_mach[1] != 0; }

/* Which machine's picture and sound gbc_run hands out (0 the first, 1 the second). */
extern "C" void gbc_link_view(unsigned m) { s_view = m ? 1u : 0u; }

/* Which machine the per-machine calls (saves, states, palettes, reset) act on. */
extern "C" void gbc_link_select(unsigned m)
{
	if (s_mach[m ? 1 : 0])
		g_gb = s_mach[m ? 1 : 0];
}

/* Test numbers: 0/1 bytes machine 0/1 clocked out, 2/3 a byte waiting at machine 0/1, 4/5 the
 * buttons port 0/1 last read, 6 the machine watched, 7 the machine selected. */
extern "C" unsigned gbc_link_stat(unsigned what)
{
	if (!s_mach[1] || what > 7)
		return 0;
	if (what >= 6)
		return what == 6 ? s_view : (g_gb == s_mach[1] ? 1u : 0u);
	if (what >= 4)
		return LkInput::s_last_pad[what - 4];
	return what < 2 ? s_end[what]->bytes : s_end[what - 2]->waiting;
}

/* Both machines, in the order described above; the same contract as gbc_run for the watched one. */
static long link_run(video_pixel_t *video, int pitch, uint_least32_t *sound, unsigned cap,
		     unsigned *samples)
{
	unsigned want = *samples, ran[2] = { 0, 0 }, out = 0, k, guard;
	long frame = -1;
	if (want + 2u * LINK_SLICE > cap)
		want = cap > 2u * LINK_SLICE ? cap - 2u * LINK_SLICE : 0u;
	while (ran[0] < want) {
		unsigned n = want - ran[0] < LINK_SLICE ? want - ran[0] : LINK_SLICE;
		long r;
		/* the first machine's slice */
		s_running = 0;
		if (s_view == 0) r = s_mach[0]->runFor(s_draw[0], LINK_W, sound + out, cap - out, n);
		else             r = s_mach[0]->runFor(s_draw[0], LINK_W, s_spare, s_spare_cap, n);
		s_running = -1;
		if (s_view == 0) out += n;
		ran[0] += n;
		s_done[0] += n;
		if (r >= 0)
			memcpy(s_shown[0], s_draw[0], sizeof(video_pixel_t) * LINK_W * LINK_H);
		/* the second catches up with it */
		for (guard = 0; s_done[1] < s_done[0] && guard < 16u; guard++) {
			unsigned m = (unsigned)(s_done[0] - s_done[1]);
			long r2;
			if (m > 2u * LINK_SLICE) m = 2u * LINK_SLICE;
			if (ran[1] + m + LINK_SLICE > cap) break;	/* the same on every console */
			s_running = 1;
			if (s_view == 1) r2 = s_mach[1]->runFor(s_draw[1], LINK_W, sound + out, cap - out, m);
			else             r2 = s_mach[1]->runFor(s_draw[1], LINK_W, s_spare, s_spare_cap, m);
			s_running = -1;
			if (s_view == 1) out += m;
			ran[1] += m;
			s_done[1] += m;
			if (r2 >= 0)
				memcpy(s_shown[1], s_draw[1], sizeof(video_pixel_t) * LINK_W * LINK_H);
			if (!m)
				break;
		}
		if (r >= 0) { frame = 0; break; }
		if (!n)
			break;
	}
	*samples = out;
	if (frame >= 0)
		for (k = 0; k < LINK_H; k++)
			memcpy(video + k * pitch, s_shown[s_view] + k * LINK_W, sizeof(video_pixel_t) * LINK_W);
	return frame;
}

/* ---- a linked session's whole state: both machines, the cable and how far apart they are ----
 * Layout: 'L' 'K' version 0, the two state sizes, the second machine's lead in samples (signed),
 * each end's waiting byte (waiting, fast, byte, 0), then the two machine states. */
#define LINK_HDR	20u

extern "C" unsigned gbc_link_state_size(void)
{
	if (!s_mach[1])
		return 0;
	return LINK_HDR + (unsigned)s_mach[0]->stateSize() + (unsigned)s_mach[1]->stateSize();
}

static void put32(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static unsigned get32(const unsigned char *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

extern "C" void gbc_link_state_save(void *buf)
{
	unsigned char *p = (unsigned char *)buf;
	unsigned a, i;
	if (!s_mach[1])
		return;
	a = (unsigned)s_mach[0]->stateSize();
	p[0] = 'L'; p[1] = 'K'; p[2] = 1; p[3] = 0;
	put32(p + 4, a);
	put32(p + 8, (unsigned)s_mach[1]->stateSize());
	put32(p + 12, (unsigned)(int)(s_done[1] - s_done[0]));
	for (i = 0; i < 2; i++) {
		p[16 + 2 * i] = (unsigned char)((s_end[i]->waiting ? 1 : 0) | (s_end[i]->fast ? 2 : 0));
		p[17 + 2 * i] = s_end[i]->byte;
	}
	s_mach[0]->saveState(p + LINK_HDR);
	s_mach[1]->saveState(p + LINK_HDR + a);
}

/* 0 loaded, -1 not a linked state or it does not fit these machines */
extern "C" int gbc_link_state_load(const void *buf, unsigned size)
{
	const unsigned char *p = (const unsigned char *)buf;
	unsigned a, b, i;
	if (!s_mach[1] || size < LINK_HDR || p[0] != 'L' || p[1] != 'K' || p[2] != 1)
		return -1;
	a = get32(p + 4);
	b = get32(p + 8);
	if (LINK_HDR + a + b > size)
		return -1;
	if (!s_mach[0]->loadState(p + LINK_HDR, a) || !s_mach[1]->loadState(p + LINK_HDR + a, b))
		return -1;
	s_done[0] = 0;
	s_done[1] = (unsigned long long)(long long)(int)get32(p + 12);
	for (i = 0; i < 2; i++) {
		s_end[i]->waiting = (p[16 + 2 * i] & 1) != 0;
		s_end[i]->fast = (p[16 + 2 * i] & 2) != 0;
		s_end[i]->byte = p[17 + 2 * i];
	}
	return 0;
}

/* ---- cheat codes (V5) ----
 * One cheat's code as a RetroArch .cht file gives it: one or more codes joined by '+', spaces,
 * commas or ';'. Each is a Game Genie code (HHH-HHH, or HHH-HHH-HHH with the compare byte), which
 * changes the ROM where the cartridge maps it, or a GameShark code (8 hex digits), written into
 * memory at each vertical blank (patches/0004). gambatte adds them to the ones in force; a new load
 * drops them. A text with any code that is neither is refused whole. */
static int cheat_hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static int cheat_is_genie(const char *c, size_t n)
{
	if (n != 7 && n != 11) return 0;
	for (size_t i = 0; i < n; i++)
		if ((i == 3 || i == 7) ? c[i] != '-' : !cheat_hex(c[i])) return 0;
	return 1;
}
static int cheat_is_shark(const char *c, size_t n)
{
	if (n != 8) return 0;
	for (size_t i = 0; i < n; i++)
		if (!cheat_hex(c[i])) return 0;
	return 1;
}
static int cheat_sep(char c) { return c == '+' || c == ' ' || c == ',' || c == ';' || c == '\t' || c == '\r' || c == '\n'; }

extern "C" void gbc_cheat_reset(void)
{
	if (g_gb) g_gb->clearCheats();
}

extern "C" int gbc_cheat_add(const char *code)
{
	if (!g_gb || !code) return -1;
	/* every code checked first, so a text with one bad code changes nothing */
	unsigned codes = 0;
	for (const char *p = code; *p;) {
		while (*p && cheat_sep(*p)) p++;
		const char *e = p;
		while (*e && !cheat_sep(*e)) e++;
		size_t n = (size_t)(e - p);
		if (!n) break;
		if (!cheat_is_genie(p, n) && !cheat_is_shark(p, n)) return -1;
		codes++;
		p = e;
	}
	if (!codes) return -1;
	for (const char *p = code; *p;) {
		while (*p && cheat_sep(*p)) p++;
		const char *e = p;
		while (*e && !cheat_sep(*e)) e++;
		size_t n = (size_t)(e - p);
		if (!n) break;
		if (cheat_is_genie(p, n)) g_gb->addGameGenie(p, (unsigned) n);
		else g_gb->addGameShark(p);
		p = e;
	}
	return 0;
}
