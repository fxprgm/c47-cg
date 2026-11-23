#!/usr/bin/env python3
"""Fetch the C47 firmware at the commit pinned in UPSTREAM, apply our patches, and run
upstream's host generators.

    python tools/sync_upstream.py            # fetch if needed, re-apply patches, regenerate
    python tools/sync_upstream.py --no-gen   # only the source tree

Result:
    upstream/          the C47 tree at the pinned commit with patches/*.patch applied
                       (git-ignored; reset on every run, so never edit it by hand: make a patch)
    build/host/        upstream's meson build for the build machine (generators only)
    build/generated/   the generated sources the add-in compiles, made big-endian for the
                       SuperH (see fix_constants_endianness)
    build/generated_le/  the same, little-endian, for the PC test build
    build/sources.cmake  upstream's source lists

The generators need, on the build machine: meson, ninja, a C compiler, freetype2, gmp,
gtk+-3.0 (upstream's meson.build requires it even for the generators) and xlsxio_xlsx2csv
(https://github.com/brechtsanders/xlsxio; not packaged by Debian/Ubuntu).
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UP = os.path.join(ROOT, "upstream")
HOST = os.path.join(ROOT, "build", "host")
GEN = os.path.join(ROOT, "build", "generated")
GEN_LE = os.path.join(ROOT, "build", "generated_le")

# ninja target (in build/host) -> file name in build/generated
GENERATED = {
    "src/c47/vcs.h": "vcs.h",
    "src/ttf2RasterFonts/rasterFontsData.c": "rasterFontsData.c",
    "src/generateConstants/constantPointers.c": "constantPointers.c",
    "src/generateConstants/constantPointers.h": "constantPointers.h",
    "src/generateConstants/constantPointers2.c": "constantPointers2.c",
    "src/generateCatalogs/softmenuCatalogs.h": "softmenuCatalogs.h",
}


def run(cmd, cwd=None, **kw):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, cwd=cwd, check=True, **kw)


def pinned():
    vals = {}
    with open(os.path.join(ROOT, "UPSTREAM")) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                k, v = line.split("=", 1)
                vals[k.strip()] = v.strip()
    return vals["url"], vals["commit"]


def fetch(url, commit):
    if not os.path.isdir(os.path.join(UP, ".git")):
        os.makedirs(UP, exist_ok=True)
        run(["git", "init", "-q"], cwd=UP)
        run(["git", "remote", "add", "origin", url], cwd=UP)
    have = subprocess.run(["git", "cat-file", "-e", commit + "^{commit}"], cwd=UP,
                          capture_output=True).returncode == 0
    if not have:
        run(["git", "fetch", "-q", "--depth", "1", "origin", commit], cwd=UP)
    # Throw away the previous run's patches and anything generated in the tree.
    run(["git", "checkout", "-q", "--force", "--detach", commit], cwd=UP)
    run(["git", "clean", "-q", "-fdx"], cwd=UP)


def apply_patches():
    pdir = os.path.join(ROOT, "patches")
    patches = sorted(p for p in os.listdir(pdir) if p.endswith(".patch"))
    for p in patches:
        run(["git", "apply", "--whitespace=nowarn", os.path.join(pdir, p)], cwd=UP)
    print(f"applied {len(patches)} patch(es)")


def generate():
    if not os.path.isfile(os.path.join(HOST, "build.ninja")):
        os.makedirs(os.path.dirname(HOST), exist_ok=True)
        run(["meson", "setup", HOST, UP, "--buildtype=custom", "-DRASPBERRY=false",
             "-DDECNUMBER_FASTMUL=true"])
    run(["ninja", "-C", HOST] + list(GENERATED))
    os.makedirs(GEN, exist_ok=True)
    for target, name in GENERATED.items():
        shutil.copyfile(os.path.join(HOST, target), os.path.join(GEN, name))
    # made by meson at configure time, not a ninja target
    shutil.copyfile(os.path.join(HOST, "src/c47/version.h"), os.path.join(GEN, "version.h"))
    # the PC test build (tools/build_host.py) is little-endian: it takes these unswapped
    if os.path.isdir(GEN_LE):
        shutil.rmtree(GEN_LE)
    shutil.copytree(GEN, GEN_LE)
    n = fix_constants_endianness(os.path.join(GEN, "constantPointers.c"))
    print(f"generated sources in {GEN} ({n} constants byte-swapped for big-endian)")


# The constants table is decNumber structs serialized by the (little-endian) build machine:
#     int32 digits, int32 exponent, uint8 bits, uint8 padding, uint16 lsu[]  (DECDPUN = 3)
# one per line as "/* name */ b,b,b,b, b,b,b,b, b, b, b,b,b,b,...". The SuperH is big-endian,
# so each int32 and each uint16 unit is swapped. The "const34_" entries in the same table are
# decQuads instead (16 bytes, reversed whole). A line that doesn't have one of those shapes
# stops the build rather than being guessed at.
ENTRY = re.compile(r"^(\s*/\*.*?\*/\s*)(.*?)(,?)\s*$")


def fix_constants_endianness(path):
    out, count, inside = [], 0, False
    with open(path) as f:
        lines = f.read().split("\n")
    for line in lines:
        if re.match(r"\s*TO_QSPI const uint8_t constants\[\] = \{", line):
            inside = True
            out.append(line.replace("const uint8_t constants[]",
                                    "const uint8_t __attribute__((aligned(4))) constants[]"))
            continue
        if inside and line.strip().startswith("};"):
            inside = False
        if inside and line.strip().startswith("/*"):
            m = ENTRY.match(line)
            body = [b.strip() for b in m.group(2).split(",") if b.strip()]
            vals = [int(b, 16) for b in body]
            if "/* const34_" in m.group(1):
                # real34_t = decQuad: the 128-bit encoding in the machine's byte order
                # (DECLITEND), so the whole thing is reversed.
                if len(vals) != 16:
                    sys.exit(f"constantPointers.c: decQuad not 16 bytes: {line.strip()}")
                sw = vals[::-1]
            else:
                if len(vals) < 12 or len(vals) % 2:
                    sys.exit(f"constantPointers.c: unexpected entry shape: {line.strip()}")
                sw = vals[3::-1] + vals[7:3:-1] + vals[8:10]
                for i in range(10, len(vals), 2):
                    sw += [vals[i + 1], vals[i]]
            txt = ",".join(f"0x{v:02x}" for v in sw)
            out.append(f"{m.group(1)}{txt},")
            count += 1
            continue
        out.append(line)
    if count == 0:
        sys.exit("constantPointers.c: no constants found (format changed?)")
    with open(path, "w") as f:
        f.write("\n".join(out))
    return count


def meson_files(path, var):
    """The file list of `var = files(...)` in a meson.build, as paths relative to its folder."""
    with open(path) as f:
        text = f.read()
    m = re.search(r"^" + re.escape(var) + r"\s*=\s*files\((.*?)\)", text, re.S | re.M)
    if not m:
        sys.exit(f"{path}: no '{var} = files(...)'")
    return re.findall(r"'([^']+)'", m.group(1))


def write_source_list():
    """build/sources.cmake: the upstream sources the add-in compiles, as upstream lists them for
    its own calculator builds (meson.build), so a file added upstream is picked up."""
    core = ["src/c47/" + f for f in meson_files(os.path.join(UP, "src/c47/meson.build"), "c47_src")]
    dec = ["dep/" + f for f in meson_files(os.path.join(UP, "dep/meson.build"), "decNumber_src")]
    hal = ["src/c47-dmcp5/" + f for f in meson_files(os.path.join(UP, "src/c47-dmcp5/meson.build"),
                                                    "c47_dmcp_src")]
    os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
    with open(os.path.join(ROOT, "build", "sources.cmake"), "w") as f:
        for name, lst in (("C47_CORE_SOURCES", core), ("C47_DECNUMBER_SOURCES", dec),
                          ("C47_DMCP_HAL_SOURCES", hal)):
            f.write(f"set({name}\n")
            for p in lst:
                f.write(f"  ${{C47_UPSTREAM}}/{p}\n")
            f.write(")\n")
    print(f"source lists: {len(core)} core, {len(dec)} decNumber, {len(hal)} hal files")


def main():
    url, commit = pinned()
    fetch(url, commit)
    apply_patches()
    write_source_list()
    if "--no-gen" not in sys.argv:
        generate()


if __name__ == "__main__":
    main()
