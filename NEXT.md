# What's next

The running list of pending work. Read it at the start of a session; update it when something
is done or found. Newest decisions at the top of each section.

## Waiting on the user

- **Try the latest build on the real calculator**: start, keys, speed of heavy work (solver,
  integration, big factorials), MENU and back, SHIFT+AC/ON off and on, manual save/load
  (I/O menu: g then 0, SAVEST / LOADST).
- **◀ and ▶**: unused so far; decide what they should do (e.g. UNDO, or a second ▲▼).
- **Releases**: pushing a git tag is refused from cloud sessions, so `tools/release.py` stops
  after putting the build on the `builds` branch (v0.1.0 is there). Decide: allow tag pushes,
  or make each release by hand on GitHub from that file.
- **Report the fxlibc strchr bug** to fxlibc (Planet Casio's forge,
  `Vhex-Kernel-Core/fxlibc`): draft in `docs/fxlibc-strchr-report.md`.

## To do

- **Upstream sync** every day or two: bump `UPSTREAM`, `python tools/sync_upstream.py`, check
  the patches still apply, build, run the tests (below), release if C47 changed.
- **SNAP** (screenshots): `create_screenshot` in `src/dmcp_sys.c` saves nothing; write a 1-bit
  BMP of the 396x224 view to `\C47\SCREENS` as the R47 does.
- **Battery** in the status bar: `get_vbat` returns a fixed 3.3 V; read the CG50's battery
  (gint has no driver; the OS keeps a value).
- **Auto power off**: 10 minutes fixed in `src/dmcp_sys.c`; follow the OS's setting.
- **4 pixels narrower than the R47**: some numbers that just fit at 400 px go to scientific
  notation at 396 (1,856 upstream tests in `drm_longint_cov.txt`). Look for a smaller fix
  than the layout patch has (e.g. the digit-group gaps).
- **The big-endian fixes upstream** (`patches/0002-big-endian-byte-order.patch`) as a merge
  request to C47, if the user wants: their `AGENTS.md` sets strict rules (style, wording,
  simulator proof, commit history).

## Done (newest first)

- 2026-11-24: the CG50 toolchain prebuilt in the private repo fxprgm/cg-toolchain, restored
  by `tools/setup_env.py` (checked: same C47.g3a as with the source-built toolchain);
  NEXT.md and CLAUDE.md; tools/release.py (pushing the tag is refused from cloud sessions, see
  "Waiting on the user").
- 2026-11-23: DMCP's file browser and dialogs (`src/dmcp_ui.c`); fxlibc's strchr replaced;
  redraw after MENU and back; first run in the emulator on the real OS; display DMA bands
  aligned to 4 rows; big-endian test of upstream's suite; 396x224 layout; first build.

## How to test (see README)

- PC: `python tools/build_host.py --mini-gmp`, then `build/c47cg_host_mini tests/<script>.txt`.
- Big-endian: `python3 tools/test_bigendian.py --arch ppc` (only the known 1,871 width
  failures, on both CPUs alike).
- Emulator: hexbinoct/casio-cg50, `emu_go/c47_probe_test.go` (needs a CG50 flash image).
