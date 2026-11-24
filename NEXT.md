# What's next

The running list of pending work. Read it at the start of a session; update it when something
is done or found. Newest decisions at the top of each section.

## Waiting on the user

- **Try the latest build on the real calculator**: start, keys, speed of heavy work (solver,
  integration, big factorials), MENU and back, SHIFT+AC/ON off and on, manual save/load
  (I/O menu: g then 0, SAVEST / LOADST).
- **◀ and ▶**: unused so far; decide what they should do (e.g. UNDO, or a second ▲▼).
- **CG_TOOLCHAIN_TOKEN** (optional, makes releases take minutes instead of an hour when the
  cache is cold): a fine-grained token, resource owner fxprgm, only the repository
  cg-toolchain, permission Contents: read-only; saved in c47-cg, Settings, Secrets and
  variables, Actions, as the repository secret CG_TOOLCHAIN_TOKEN.
- **Report the fxlibc strchr bug** to fxlibc (Planet Casio's forge,
  `Vhex-Kernel-Core/fxlibc`): draft in `docs/fxlibc-strchr-report.md`.

## To do

- **Upstream sync**: a scheduled cloud task checks every second day at 02:23 UTC whether C47's
  master moved; only then does it sync, test and push (or leave a `sync-<date>` branch if
  something broke), and the user gets its report. The push of UPSTREAM makes GitHub build
  and publish the release (.github/workflows/release.yml).
  By hand: CLAUDE.md "Syncing with upstream C47".
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
  NEXT.md and CLAUDE.md; releases built and published by GitHub Actions
  (.github/workflows/release.yml); the emulator's flash image in the private repo
  fxprgm/cg-flashfile.
- 2026-11-23: DMCP's file browser and dialogs (`src/dmcp_ui.c`); fxlibc's strchr replaced;
  redraw after MENU and back; first run in the emulator on the real OS; display DMA bands
  aligned to 4 rows; big-endian test of upstream's suite; 396x224 layout; first build.

## How to test (see README)

- PC: `python tools/build_host.py --mini-gmp`, then `build/c47cg_host_mini tests/<script>.txt`.
- Big-endian: `python3 tools/test_bigendian.py --arch ppc` (only the known 1,871 width
  failures, on both CPUs alike).
- Emulator: hexbinoct/casio-cg50, `emu_go/c47_probe_test.go` (needs a CG50 flash image).
