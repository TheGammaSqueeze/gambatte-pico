# gambatte-pico

> **All of the emulation here is gambatte's.** gambatte is Sindre Aamås's (sinamas) accuracy
> focused Game Boy and Game Boy Color emulator, and this build uses the libretro team's port of
> it, [gambatte-libretro](https://github.com/libretro/gambatte-libretro), maintained by the libretro
> team and its contributors. If you enjoy it, the credit belongs to them.

[gambatte](https://github.com/libretro/gambatte-libretro), built as a loadable Game Boy / Game Boy
Color core for the GammaOS bootloader emulator on the Konkr Pocket Advance (MediaTek MT6785,
`lk-mt6785`, branch `lk-gba-emu-sd-card-kpa`).

This repository adds no emulation code: it holds the upstream tree as a submodule, one small patch,
the glue that lets gambatte run inside a bootloader with no operating system, and a build script.

**This is an experimental project and does not accept issues.** Please do not report problems you
see with gambatte-pico to gambatte or gambatte-libretro upstream either: this build runs gambatte in
a bootloader on hardware upstream does not target, so its behaviour here says nothing about
gambatte itself.

## Credits

- **gambatte** (Sindre Aamås and contributors, GPL-2.0): the emulator library, `libgambatte/src`,
  its CPU, memory and cartridge controllers, real time clock, sound channels and the PPU.
- **gambatte-libretro** (the libretro team and contributors, GPL-2.0): the port this build is taken
  from, including its libretro specific video and cartridge code and the colour correction.
- The Game Boy colourisation palette catalogue in `libgambatte/libretro/gbcpalettes.h`: the Game
  Boy, Game Boy Color and Super Game Boy palettes, and the community packs by **TheWolfBunny64**
  (TWB64) and **PixelShift**. The LK's palette menu browses this list, and its Auto mode picks the
  per game palette the Game Boy Color would by the ROM's title.

## License

gambatte is licensed under the GNU General Public License, version 2 only (`LICENSE`, the same
text as upstream's `COPYING`). The glue in `pico/`, the patch in `patches/` and the build script
are GPL-2.0 as well, so the blob as a whole is under GPL-2.0. Each file keeps its own notice. The
blob also links a few routines from newlib's libm (`powf` and its helpers, permissive licence) and
libgcc's division and soft float helpers (GCC Runtime Library Exception), and the C++ templates
gambatte uses (`std::vector`, `std::unique_ptr`, heap sort) are instantiated from libstdc++'s
headers; `THIRD_PARTY_NOTICES.md` lists everything. The GPL asks that whoever receives the blob can
get its complete source: this repository with the submodule at the commit the blob was built
from is that source.

## Layout

| Path | What it is |
| --- | --- |
| `upstream/` | gambatte-libretro, as a git submodule at the commit the LK's copy was taken from, never modified in place |
| `patches/` | changes to upstream, applied by `build.sh` to a copy in `out/src` |
| `pico/pico_core.cpp` | the LK's core interface (`gbc_core_abi.h`, version 1) filled in for gambatte: the export table, the blob's entry point, the palette catalogue |
| `pico/pico_bridge.cpp` | `extern "C"` functions over gambatte's `GB` class, so only the C ABI crosses into the LK |
| `pico/pico_runtime.cpp` | the C++ runtime: `operator new` over the LK's heap, the libstdc++ error hooks, empty logging and rumble |
| `pico/pico_libc.c` | `memcpy`, `memmove`, `memset`, `strcmp` and `errno` for libm |
| `pico/gbc_core_abi.h` | a copy of `lk-mt6785/emu/gbc/gbc_core_abi.h`; the LK's copy is the reference |
| `pico/blob.ld` | link script: flat image, fixed address, 20 byte header |
| `build.sh` | builds `out/gambatte-pico.blob` |
| `THIRD_PARTY_NOTICES.md` | what is in the blob and under which licence |

### Patches

| Patch | Why |
| --- | --- |
| `0001-no-cheat-code-parsing.patch` | `Interrupter::setGameShark` and `Cartridge::setGameGenie` keep their signatures and do nothing. The bootloader has no cheat menu, and the two code parsers are the only code in the core that builds and slices `std::string`, whose members (`substr`, `find`, `_M_dispose`, `_S_copy`) live in libstdc++'s library, which the blob does not link. Without this patch the link fails on exactly those four symbols. |

## Building

Needs `arm-none-eabi-gcc` and `arm-none-eabi-g++` with newlib and libstdc++'s headers (tested with
10.3-2021.07 and newlib 3.3.0, Ubuntu 22.04's `gcc-arm-none-eabi`, `libnewlib-arm-none-eabi` and
`libstdc++-arm-none-eabi-newlib`), `patch`, `python3` and git.

```
git clone --recurse-submodules <this repository>   # or: git submodule update --init upstream
./build.sh
```

Output: `out/gambatte-pico.blob`, plus the ELF and a link map. The script prints the size, the
memory the core takes with its BSS, and the SHA-256.

The build settings are the LK's own: ARMv7-A, soft float, interworking, freestanding, `-Os`, no
exceptions, RTTI, thread safe statics or `__cxa_atexit`, plain sized enums, no unaligned
accesses, and `__LIBRETRO__` and `VIDEO_RGB565` (gambatte draws 16 bit RGB565 pixels). The link
takes the objects in a fixed order, then libm and libgcc from the multilib these flags select
(`thumb/v7-a/nofp`). With the toolchain above, the blob is byte for byte the `core_gbc.blob` the LK's
`emu/gbc/build_core.sh` and `build_core_blob.sh` built from its vendored copy of gambatte:

```
146309 bytes, sha256 43be16ce2874b1c6c6d399f704971d3dc2516f5fef4188638a0aadf6b2987267
```

No path ends up in the blob (nothing is built with debug information, the debug sections libm and
libgcc carry are not loaded, and nothing uses `__FILE__`), so it does not depend on where the
repository is checked out. A different compiler
or newlib version gives a different, equally valid blob.

`EXTRA_PATCHES=/path/a.patch ./build.sh` applies more patches after `patches/` for experiments.
Releases use none.

## Installing

Copy `out/gambatte-pico.blob` to the SD card as `/cores/gambatte-pico.blob`.

## How the LK uses it

- When a Game Boy or Game Boy Color game is started, the LK reads `/cores/gambatte-pico.blob` from
  the card into the GB/GBC core slot at 0x4E800000 (the address the blob is linked for, above the
  GBA core slot at 0x4E400000 and below the shared emulator arena at 0x50000000). It checks the
  header (magic `GBC1`, version 1, entry offset, load size and span), zeroes everything from the
  load size up to the span (the BSS), cleans the data cache and invalidates the instruction cache
  over the image, and calls the entry with its imports table. The entry returns the export table
  the driver uses from then on.
- The driver then gives the core its heap in the shared arena (`heap_init`), creates the machine
  (`create`), loads the ROM from the buffer it read it into (`load`, with `FORCE_DMG` for `.gb`
  files) and runs it a frame at a time (`run`: 160x144 RGB565 pixels and stereo samples at
  2097152 Hz, which the LK resamples to 48 kHz).
- Battery saves and the cartridge clock are buffers the LK reads and writes on the card
  (`savedata_*`, `rtcdata_*`); save states, run ahead and rewind use `state_size`, `state_save`
  and `state_load` on buffers the LK owns.
- The Pico menu's palette list, Auto palette, colour correction and dark filter go through the
  remaining exports.

## Target notes for maintainers

- **The interface.** `pico/gbc_core_abi.h` is the contract with the LK: the functions the LK's GB/GBC
  driver calls (`gbc_core_exports`) and the two services the LK gives the core (`gbc_core_imports`:
  the pad state for gambatte's input getter, and the wall clock in seconds for the cartridge real
  time clock, which `time()` in `pico/pico_core.cpp` forwards). Keep the copy here identical to the
  LK's.
- **Memory.** The blob itself is about 143 KiB with its BSS. Everything else (the machine, the
  cartridge RAM, the ROM's banks) comes from `operator new`, a bump allocator over the region the LK
  passes to `heap_init`; `delete` does nothing, since the core allocates once per game and the LK
  sets the heap again before the next one.
- **No constructors run.** The LK calls the entry directly; there is no C runtime start up, and the
  link script drops `.init_array`. gambatte needs no static constructors with these flags (no
  thread safe statics, no `__cxa_atexit`), and the core is built at run time by `create`.
- **No exceptions.** libstdc++'s `__throw_*` hooks spin forever: with a valid ROM and a large
  enough heap they are never reached.
- **Floating point.** Soft float, as the LK is built: `powf` (colour correction) comes from newlib's
  libm and the arithmetic helpers from libgcc. Floating point turns palette colours into pixel values
  (colour correction, dark filter) when a palette changes or the screen is blanked, never per
  pixel.
- **The link order.** `build.sh` lists the objects in the order the LK's build linked them (its
  object directory sorted by name), which is what makes the blob identical to the LK built one.
  Adding or renaming a file only needs that list kept complete; a new order gives a valid blob with
  a different layout.
