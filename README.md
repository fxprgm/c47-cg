# c47-cg

A port of the [C47](https://gitlab.com/rpncalculators/c43) RPN calculator firmware, in its R47
form, to the Casio fx-CG50 as a gint add-in.

Status: in development. It runs in a PC test build of the add-in code; the calculator build is
next.

## How it works

- **Upstream C47, unchanged except for patches.** `tools/sync_upstream.py` fetches the commit
  pinned in `UPSTREAM` into `upstream/`, applies `patches/*.patch`, and runs upstream's own
  generators (fonts, constants, catalogs). The fonts are the R47's, generated from upstream's
  TrueType files by upstream's tool.
- **The CG50 is big-endian,** the R47 little-endian: the generated constants table is
  byte-swapped by the sync script and decNumber is built with `DECLITEND=0`.
- **A DMCP layer** (`src/dmcp_*.c`): C47 is built as for the R47 hardware (`DMCP_BUILD`,
  `NEW_HW`, R47 keyboard) and calls SwissMicros's DMCP system functions; these implement
  them: the 400x240 one-bit screen buffer, the key buffer, sleep and timers, the clock, and
  FatFs-style files on the storage memory (in `\\fls0\C47\`).
- **The screen:** `patches/0001-layout-396x224.patch` lays C47 out for the CG50's 396x224:
  status bar and softkey rows as on the R47, stack lines 32 px instead of 36, full-screen
  views fitted.
- **The platform** (`src/plat.h`): `src/plat_gint.c` on the calculator, `src/plat_host.c` for a
  PC test build that runs a key script on a simulated clock and writes screenshots.
- **Big integers:** GMP's mini-gmp (`src/gmp/`) with the three functions C47 needs that it
  lacks.

Keys: [docs/KEYS.md](docs/KEYS.md).

## Building

Needs, on the build machine: Python 3, meson, ninja, a C compiler, freetype2, gmp, gtk+-3.0
(upstream's meson.build wants it even for the generators) and
[xlsxio](https://github.com/brechtsanders/xlsxio)'s `xlsxio_xlsx2csv`.

    python tools/sync_upstream.py         # upstream + patches + generated sources
    fxsdk build-cg                        # the add-in, C47.g3a (fxSDK + gint)
    python tools/build_host.py --mini-gmp # the PC test build, build/c47cg_host_mini
    build/c47cg_host_mini tests/smoke.txt # run a key script, write screenshots

## License

GPL-3.0, as the upstream firmware. `src/dmcp/` holds SwissMicros's DMCP SDK headers
(BSD-3-Clause, adapted); `src/gmp/mini-gmp.*` is from GMP (LGPLv3 / GPLv2+).

This is an independent community project, not affiliated with SwissMicros or Casio.
