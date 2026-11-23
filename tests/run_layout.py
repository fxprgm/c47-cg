#!/usr/bin/env python3
"""Run the tests/layout_*.txt screen scripts and turn their screenshots into PNGs.

    python3 tests/run_layout.py [BINARY [OUTDIR]] [-k NAME]

BINARY defaults to build/c47cg_host, OUTDIR to tests/out/layout/after. Each script runs in OUTDIR
with a fresh, empty files folder (so a run doesn't see the state the previous one saved), and
its `shot x.ppm` / `shotfull x.ppm` land there as x.png. The host build prints a WARNING for a
shot where C47 drew below the 224 rows the CG50 shows; those are collected at the end.

With two output folders already made (e.g. one from an unpatched build into .../before),
`--compare BEFORE AFTER DEST` puts each pair side by side (C47's full 400x240 before, left;
the CG50's 396x224 after, right) for review.
"""
import glob
import os
import shutil
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def run(binary, outdir, only=None):
    os.makedirs(outdir, exist_ok=True)
    warnings = []
    for script in sorted(glob.glob(os.path.join(HERE, "layout_*.txt"))):
        name = os.path.basename(script)
        if only and only not in name:
            continue
        files = os.path.join(outdir, "files_" + name[:-4])
        shutil.rmtree(files, ignore_errors=True)
        r = subprocess.run([binary, script, files], cwd=outdir, capture_output=True, text=True,
                           timeout=600)
        shutil.rmtree(files, ignore_errors=True)
        for line in r.stderr.splitlines():
            if "WARNING" in line or "script:" in line:
                warnings.append(f"{name}: {line.strip()}")
        print(f"{name}: exit {r.returncode}, {r.stderr.count(' shot')} shot(s)")
    n = 0
    for ppm in glob.glob(os.path.join(outdir, "*.ppm")):
        Image.open(ppm).save(ppm[:-4] + ".png")
        os.remove(ppm)
        n += 1
    print(f"{n} PNG(s) in {outdir}")
    for w in warnings:
        print(w)


def compare(before, after, dest):
    os.makedirs(dest, exist_ok=True)
    for a in sorted(glob.glob(os.path.join(after, "*.png"))):
        name = os.path.basename(a)
        b = os.path.join(before, name.replace(".png", "_full.png"))
        if not os.path.exists(b):
            b = os.path.join(before, name)
        if not os.path.exists(b):
            continue
        ia, ib = Image.open(a), Image.open(b)
        s = 2
        out = Image.new("RGB", ((ib.width + ia.width + 12) * s, max(ib.height, ia.height) * s), (90, 90, 90))
        out.paste(ib.resize((ib.width * s, ib.height * s), Image.NEAREST), (0, 0))
        out.paste(ia.resize((ia.width * s, ia.height * s), Image.NEAREST), ((ib.width + 12) * s, 0))
        out.save(os.path.join(dest, name))
    print(f"side by side in {dest}")


def main():
    args = sys.argv[1:]
    if args and args[0] == "--compare":
        compare(*args[1:4])
        return
    only = None
    if "-k" in args:
        i = args.index("-k")
        only = args[i + 1]
        del args[i:i + 2]
    binary = os.path.abspath(args[0]) if args else os.path.join(ROOT, "build", "c47cg_host")
    outdir = os.path.abspath(args[1]) if len(args) > 1 else os.path.join(HERE, "out", "layout", "after")
    run(binary, outdir, only)


if __name__ == "__main__":
    main()
