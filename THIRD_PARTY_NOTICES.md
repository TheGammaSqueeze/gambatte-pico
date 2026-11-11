# Third party notices

The gambatte-pico blob (`gambatte-pico.blob`) is built from the following. As a whole it is under
the GNU General Public License, version 2 (`LICENSE`). The libretro headers, newlib's libm and
libgcc listed below are under permissive licences or licence exceptions that only ask for their
notices to be kept.

## gambatte and gambatte-libretro (GPL-2.0)

- Source: <https://github.com/libretro/gambatte-libretro>, the `upstream/` submodule, at the commit
  recorded in this repository (d9d6cd06382d1ced30de34d56d3609452323dab1, the commit the LK's
  vendored copy was taken from).
- Copyright (C) 2007 and later Sindre Aamås (sinamas) and the gambatte contributors; the libretro
  port by the libretro team and its contributors. Licensed under the GNU General Public License
  version 2 as published by the Free Software Foundation (upstream `COPYING`, the same text as
  `LICENSE` here).
- Compiled: the emulator library `libgambatte/src` (with `patches/0001-no-cheat-code-parsing.patch`
  applied), its headers in `libgambatte/include` and `common/`, `libgambatte/libretro/gambatte_log.h`
  and the palette catalogue `libgambatte/libretro/gbcpalettes.h`. The libretro frontend
  (`libgambatte/libretro/libretro.cpp` and the rest of that folder) is not built.
- The palette catalogue holds the Game Boy, Game Boy Color and Super Game Boy palettes and two
  community packs: the TWB64 palettes by TheWolfBunny64
  (<https://www.deviantart.com/thewolfbunny64/gallery/69987002/game-boy-palettes>) and the
  PixelShift palettes (<https://github.com/libretro/gambatte-libretro/issues/219>), as credited in
  that file.

## libretro-common headers (MIT style licence)

`libgambatte/libretro-common/include/libretro.h` and `libgambatte/libretro-common/include/array/rhmap.h`,
Copyright (C) 2010-2020 The RetroArch team, are included while compiling. Each carries its own
permissive notice ("Permission is hereby granted, free of charge, ..."), which applies to that file
only. No code from `rhmap.h` ends up in the blob.

## This repository (GPL-2.0)

The glue in `pico/`, the patch in `patches/` and `build.sh`: Copyright (c) 2026 TheGammaSqueeze,
GPL-2.0 (`LICENSE`). `pico/gbc_core_abi.h` is a copy of the interface header of the GammaOS LK
(lk-mt6785) by the same author, provided here under GPL-2.0.

## The C and C++ runtime pieces in the blob

- **newlib's libm** (the fdlibm based single precision routines, Copyright (C) 1993 Sun
  Microsystems, Inc. and the newlib contributors): `powf` and what it calls (`__ieee754_powf`,
  `__ieee754_sqrtf`, `fabsf`, `finitef`, `nanf`, `rintf`, `scalbnf`, `__fdlib_version`). Their
  notice: "Developed at SunPro, a Sun Microsystems, Inc. business. Permission to use, copy, modify,
  and distribute this software is freely granted, provided that this notice is preserved." The
  full set of newlib notices is in newlib's `COPYING.NEWLIB` and, for Ubuntu's
  `libnewlib-arm-none-eabi`, in `/usr/share/doc/libnewlib-arm-none-eabi/copyright`. The link map
  (`out/gambatte-pico.map`) lists every object taken from the library.
- **libgcc** (the GCC project): integer division and soft float helpers (`__aeabi_idiv`,
  `__aeabi_uidiv`, `__aeabi_ldivmod`, `__aeabi_uldivmod`, `__udivmoddi4`, `__aeabi_fadd`,
  `__aeabi_fmul`, `__aeabi_fdiv`, the float comparisons, `__aeabi_dadd` and `__aeabi_d2uiz`).
  GPL-3.0 with the GCC Runtime Library Exception, which places no conditions on the compiled blob.
- **libstdc++ headers** (the GCC project): gambatte's use of `std::vector`, `std::unique_ptr` and
  the standard sort and heap algorithms is instantiated from libstdc++'s headers at compile time.
  No libstdc++ library code is linked. GPL-3.0 with the GCC Runtime Library Exception, which places
  no conditions on the compiled blob.

## Corresponding source

The GPL asks that whoever receives the blob can obtain its complete source. That is this
repository with the upstream submodule at the commit recorded here; `./build.sh` rebuilds the blob
from it.
