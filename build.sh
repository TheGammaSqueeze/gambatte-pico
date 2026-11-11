#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 TheGammaSqueeze
# gambatte-pico: build the gambatte Game Boy / Game Boy Color core blob for the GammaOS LK
# (lk-mt6785, Konkr Pocket Advance).
#
#   ./build.sh            -> out/gambatte-pico.blob (copy it to the SD card as /cores/gambatte-pico.blob)
#   ./build.sh clean
#
# The upstream tree (submodule upstream/, libretro/gambatte-libretro) is never modified: its
# libgambatte/ and common/ are copied to out/src, the patches in patches/ are applied there, and
# that copy is compiled together with the glue in pico/. Only the emulator library (libgambatte/src)
# is built; the libretro frontend (libgambatte/libretro/libretro.cpp) is not, but its palette
# catalogue (gbcpalettes.h) is compiled into pico/pico_core.cpp.
#
# Target: the LK runs the core in 32 bit ARM state, soft float, as the LK itself is built (ARMv7-A,
# EABI, interworking), -Os, no exceptions or RTTI. The flags, the source list and the link order are
# the ones the LK's own emu/gbc build used, so the blob is byte for byte the one the LK built.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
OUT=$ROOT/out
CXX=${CXX:-arm-none-eabi-g++}
CC=${CC:-arm-none-eabi-gcc}
LD=${LD:-arm-none-eabi-ld}
OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}

if [ "${1:-}" = clean ]; then rm -rf "$OUT"; exit 0; fi

if [ ! -f upstream/libgambatte/src/gambatte.cpp ]; then
	git submodule update --init upstream
fi

UPSTREAM_REV=$(git -C upstream describe --tags --always 2>/dev/null || git -C upstream rev-parse --short HEAD)
PICO_REV=$(git describe --always --dirty 2>/dev/null || echo unknown)
VERSION="gambatte ${UPSTREAM_REV} (pico ${PICO_REV})"

rm -rf "$OUT/src" "$OUT/obj"
mkdir -p "$OUT/src" "$OUT/obj"
cp -a upstream/libgambatte upstream/common "$OUT/src/"
# EXTRA_PATCHES: more patches for experiments, applied after these; releases use none
for p in patches/*.patch ${EXTRA_PATCHES:-}; do
	[ -e "$p" ] || continue
	echo ">> patch $(basename "$p")"
	case "$p" in /*) pp=$p;; *) pp=$ROOT/$p;; esac
	(cd "$OUT/src" && patch -p1 --quiet --forward < "$pp")
done

CORE=$OUT/src/libgambatte
INC="-I$CORE/src -I$CORE/include -I$OUT/src/common -I$CORE/libretro -I$CORE/libretro-common/include"
ARCH="-march=armv7-a -mfloat-abi=soft -mthumb-interwork -ffreestanding"
CXXFLAGS="$ARCH -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -fno-short-enums \
	-mno-unaligned-access -Os -fno-common -D__LIBRETRO__ -DVIDEO_RGB565 $INC"
# the C runtime is built -fno-builtin, as any memcpy/memset must be: GCC otherwise turns their
# byte loops back into calls to themselves
CFLAGS="$ARCH -fno-builtin -fno-common -fno-short-enums -Os"

# gambatte's emulator library (upstream libgambatte/src)
SRCS="bootloader cpu gambatte initstate interrupter interruptrequester
	gambatte-memory sound statesaver tima video video_libretro
	mem/cartridge mem/cartridge_libretro mem/huc3 mem/memptrs mem/rtc
	sound/channel1 sound/channel2 sound/channel3 sound/channel4
	sound/duty_unit sound/envelope_unit sound/length_counter
	video/ly_counter video/lyc_irq video/next_m0_time video/ppu
	video/sprite_mapper"
for f in $SRCS; do
	$CXX $CXXFLAGS -c "$CORE/src/$f.cpp" -o "$OUT/obj/core_$(echo "$f" | tr '/' '_').o"
done
# the glue
$CXX $CXXFLAGS -c "$ROOT/pico/pico_runtime.cpp" -o "$OUT/obj/pico_runtime.o"
$CXX $CXXFLAGS -c "$ROOT/pico/pico_bridge.cpp" -o "$OUT/obj/pico_bridge.o"
# gbcpalettes.h brings title lookups pico_core.cpp does not use, hence -Wno-unused-function
$CXX $CXXFLAGS -I"$ROOT/pico" -Wall -Wno-unused-function -c "$ROOT/pico/pico_core.cpp" -o "$OUT/obj/pico_core.o"
$CC $CFLAGS -I"$ROOT/pico" -Wall -c "$ROOT/pico/pico_libc.c" -o "$OUT/obj/pico_libc.o"

# The link order decides where every function lands. It is the order the LK's build linked its
# objects in (its object directory, sorted by name), so that the blob stays identical to the one
# the LK built: keep it when adding or renaming files, or the blob changes layout (harmless, but
# no longer comparable byte for byte).
LINK="core_bootloader core_cpu core_gambatte-memory core_gambatte pico_libc pico_core
	pico_runtime pico_bridge core_initstate core_interrupter core_interruptrequester
	core_mem_cartridge core_mem_cartridge_libretro core_mem_huc3 core_mem_memptrs core_mem_rtc
	core_sound core_sound_channel1 core_sound_channel2 core_sound_channel3 core_sound_channel4
	core_sound_duty_unit core_sound_envelope_unit core_sound_length_counter core_statesaver
	core_tima core_video core_video_libretro core_video_ly_counter core_video_lyc_irq
	core_video_next_m0_time core_video_ppu core_video_sprite_mapper"
OBJS=()
for o in $LINK; do OBJS+=("$OUT/obj/$o.o"); done
[ "${#OBJS[@]}" = "$(ls "$OUT"/obj/*.o | wc -l)" ] || { echo "the link list does not name every object" >&2; exit 1; }

# newlib's libm (powf, for colour correction) and libgcc (division, soft float), from the
# multilib that matches the flags above; nothing else from the toolchain is linked
LIBM=$($CC $CFLAGS -print-file-name=libm.a)
LIBGCC=$($CC $CFLAGS -print-libgcc-file-name)
# libm's objects are built with short enums and the blob with 32 bit ones; the linker warns about
# each, but no enum crosses between them (powf takes and returns floats)
$LD -T "$ROOT/pico/blob.ld" -Map "$OUT/gambatte-pico.map" "${OBJS[@]}" "$LIBM" "$LIBGCC" \
	-o "$OUT/gambatte-pico.elf" 2> >(grep -v 'uses variable-size enums yet the output is to use 32-bit enums' >&2)
$OBJCOPY -O binary "$OUT/gambatte-pico.elf" "$OUT/gambatte-pico.blob"

python3 - "$OUT/gambatte-pico.blob" "$VERSION" <<'EOF'
import struct, sys, hashlib
d = open(sys.argv[1], 'rb').read()
magic, ver, entry, load, span = struct.unpack_from('<5I', d)
assert magic == 0x31434247 and ver == 1, "bad header"
assert load == len(d), "load size %d != file %d" % (load, len(d))
print("gambatte-pico.blob: %s" % sys.argv[2])
print("  file %d bytes, span %d bytes (%.1f KiB with BSS), entry +0x%x" % (len(d), span, span / 1024.0, entry))
print("  sha256 %s" % hashlib.sha256(d).hexdigest())
EOF
