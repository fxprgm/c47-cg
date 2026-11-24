# c47-cg: C47 (R47) for the fx-CG50

**First, every session: read `NEXT.md`.** It is the list of pending work and what is waiting
on the user. Update it when something is done or found, in the same commit.

README.md explains how the port works; docs/KEYS.md the keys.

## Rules

- `upstream/` is generated (a checkout of the pinned C47 commit with `patches/` applied) and
  reset by every `tools/sync_upstream.py`: never edit it in place to keep a change. A change to
  C47 is a patch in `patches/` (edit in `upstream/`, then `git -C upstream diff` into the patch,
  as described in the patch files' history), re-applied by the sync script.
- `upstream/AGENTS.md` is C47's rule book for contributions to C47 itself (merge requests to
  gitlab.com/rpncalculators/c43). It applies to text and code sent there, not to this repo.
- Keep the tests green: the PC build (`tools/build_host.py --mini-gmp` and the key scripts in
  `tests/`), and after a C47 update the big-endian run (`tools/test_bigendian.py --arch ppc`,
  which must fail only the known width-dependent tests, identically on both CPUs).

## Toolchain

fxSDK + gint + fxlibc + sh-elf-gcc, installed with GiteaPC into `~/.local` (bin on PATH).
Building it takes over an hour, mostly fetching from Planet Casio's forge, which sometimes
answers 503: retry. The PC side also needs meson, ninja, freetype2, gmp, gtk+-3.0 and
xlsxio's `xlsxio_xlsx2csv` (built from https://github.com/brechtsanders/xlsxio).

## Syncing with upstream C47

1. Set `commit=` in `UPSTREAM` to upstream's newest master
   (`git ls-remote https://gitlab.com/rpncalculators/c43.git refs/heads/master`).
2. `python3 tools/sync_upstream.py`: if a patch no longer applies, rework it on the new code.
3. `python3 tools/build_host.py --mini-gmp`, run the key scripts; `fxsdk build-cg`;
   `python3 tools/test_bigendian.py --arch ppc`.
4. Commit (UPSTREAM, any patch changes, NEXT.md), push, and release if anything changed.

## Releasing

`python3 tools/release.py X.Y.Z` (`--pre` for a pre-release): it builds C47.g3a here, puts it
on the `builds` branch and pushes tag vX.Y.Z; `.github/workflows/release.yml` publishes the
release. Versions: the last number for an upstream sync or a fix, the middle one for new
features.

## The emulator

The add-in is run in the fx-CG50 emulator of hexbinoct/casio-cg50 (`emu_go/c47_probe_test.go`,
see that repo's RECON_NOTES.md cont.27): it needs a CG50 flash image, which is not in any
public repo.
