#!/usr/bin/env python3
"""Build the PC test build of the add-in: C47 + our DMCP layer + src/plat_host.c, with the
same defines as the calculator build (DMCP_BUILD, NEW_HW, R47 model) except 64-bit.

    python tools/build_host.py          # -> build/c47cg_host
    python tools/build_host.py -c       # clean first

Needs tools/sync_upstream.py to have run, gcc and libgmp-dev.
"""
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UP = os.path.join(ROOT, "upstream")
OBJ = os.path.join(ROOT, "build", "host_obj")
OUT = os.path.join(ROOT, "build", "c47cg_host")

OURS = ["src/dmcp_lcd.c", "src/dmcp_sys.c", "src/dmcp_ff.c", "src/view.c", "src/plat_host.c"]

CFLAGS = [
    "-O1", "-g", "-fsigned-char", "-fno-strict-aliasing", "-w",
    "-DDMCP_BUILD", "-DOS64BIT", "-DNEW_HW", "-DDECNUMBER_FASTMUL=1", "-DCALCMODEL=USER_R47",
    "-DC47CG_HOST",
    '-D__weak=__attribute__((weak))', '-D__packed=__attribute__((__packed__))',
    "-I" + os.path.join(ROOT, "src"),
    "-I" + os.path.join(ROOT, "src", "dmcp"),
    "-I" + os.path.join(ROOT, "build", "generated_le"),
    "-I" + os.path.join(UP, "src", "c47"),
    "-I" + os.path.join(UP, "dep", "decNumberICU"),
]


def sources():
    with open(os.path.join(ROOT, "build", "sources.cmake")) as f:
        ups = re.findall(r"\$\{C47_UPSTREAM\}/(\S+)", f.read())
    gen = [os.path.join(ROOT, "build", "generated_le", n)
           for n in ("rasterFontsData.c", "constantPointers.c", "constantPointers2.c")]
    return [os.path.join(UP, p) for p in ups] + gen + [os.path.join(ROOT, p) for p in OURS]


def compile_one(src):
    obj = os.path.join(OBJ, os.path.relpath(src, ROOT).replace("/", "__") + ".o")
    if os.path.exists(obj) and os.path.getmtime(obj) > os.path.getmtime(src) and not src.startswith(os.path.join(ROOT, "src")):
        return obj, None
    r = subprocess.run(["gcc", "-c", src, "-o", obj] + CFLAGS, capture_output=True, text=True)
    return obj, (r.stderr if r.returncode else None)


def main():
    if "-c" in sys.argv:
        subprocess.run(["rm", "-rf", OBJ])
    os.makedirs(OBJ, exist_ok=True)
    srcs = sources()
    errors = []
    objs = []
    with ThreadPoolExecutor(os.cpu_count()) as ex:
        for (obj, err), src in zip(ex.map(compile_one, srcs), srcs):
            objs.append(obj)
            if err:
                errors.append((src, err))
    for src, err in errors:
        print(f"--- {os.path.relpath(src, ROOT)}\n{err}")
    if errors:
        sys.exit(f"{len(errors)} file(s) failed")
    r = subprocess.run(["gcc", "-o", OUT] + objs + ["-lgmp", "-lm"], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
        sys.exit("link failed")
    print(f"built {OUT} from {len(objs)} files")


if __name__ == "__main__":
    main()
