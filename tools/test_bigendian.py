#!/usr/bin/env python3
"""Run upstream C47's own test suite (src/testSuite) on a big-endian CPU emulated with
qemu-user, next to the same suite built natively (little-endian x86-64) as the reference.

    python3 tools/test_bigendian.py                 # s390x: build + run the four variants, summary
    python3 tools/test_bigendian.py --arch ppc -v be   # 32-bit big-endian PowerPC, the fixed setup
    python3 tools/test_bigendian.py -v le,be        # only some variants
    python3 tools/test_bigendian.py --only numextra_cov,graphs_cov   # only some test files
    python3 tools/test_bigendian.py --files 40      # only the first 40 files of the list
    python3 tools/test_bigendian.py -j 4            # parallel chunks (default: all cores)

Variants:
    le          native x86-64, DECLITEND=1, build/generated_le constants      - the reference
    be          big-endian, DECLITEND=0, build/generated (byte-swapped) constants - the add-in's setup
    be-noswap   big-endian, DECLITEND=0, build/generated_le constants  (the constants fix left out)
    be-litend   big-endian, DECLITEND=1, build/generated constants     (the DECLITEND fix left out)
The default is all four. Exit status 0 only if every variant run passes everything.

Targets (--arch):
    s390x   64-bit big-endian (OS64BIT, like the reference): GMP from Ubuntu's s390x multiarch
    ppc     32-bit big-endian (OS32BIT: 32-bit long and pointers, as on the SuperH): GMP 6.3.0
            built from source once (downloaded from ftp.gnu.org) into build/betest/gmp-ppc

Needs tools/sync_upstream.py to have run (upstream/ with patches/ applied, build/generated*).
conversions.txt / conversionsSI.txt in upstream's tests are made by upstream's
src/generateTests/generateTests.py, which this script runs when they are missing.
On Ubuntu 24.04 (amd64):

    apt install gcc qemu-user gcc-s390x-linux-gnu gcc-powerpc-linux-gnu libgmp-dev
    # s390x GMP via multiarch: restrict the existing sources in
    # /etc/apt/sources.list.d/ubuntu.sources to "Architectures: amd64", add
    #   Types: deb / URIs: http://ports.ubuntu.com/ubuntu-ports/ /
    #   Suites: noble noble-updates noble-security / Components: main universe /
    #   Architectures: s390x / Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
    # as /etc/apt/sources.list.d/ports.sources, then:
    dpkg --add-architecture s390x && apt update && apt install libgmp-dev:s390x

GTK is replaced by a stub (the suite only names GtkWidget/GdkEventKey and calls
g_get_monotonic_time / g_get_real_time) and the binaries are linked statically, so qemu needs
no target root. The suite is built as upstream's src/testSuite/meson.build builds it
(PC_BUILD, LINUX, TESTSUITE_BUILD, real GMP; -O2 here instead of -O0 for qemu's sake, plus
-fsigned-char -fno-strict-aliasing as the add-in). mini-gmp (the add-in's GMP) is not used:
it passes size 0 to the free hook, so the suite's GMP leak accounting fails every file.

The list is cut into contiguous chunks run in parallel, each in its own working directory (the
suite writes its files to the current directory; res/ is linked in). Every variant uses the same
chunks, so per-file failures compare directly with le's.

Output: build/betest/le, build/betest/<arch>-<variant> (objects, binary, chunk*.log), a summary,
and per variant the test files whose failing cases (file line numbers) differ from le's.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UP = os.path.join(ROOT, "upstream")
OUT = os.path.join(ROOT, "build", "betest")
LIST = os.path.join(UP, "src", "testSuite", "tests", "testSuiteList.txt")

# big-endian targets: cross gcc, qemu, word size, where GMP comes from
ARCHES = {
    "s390x": {"cc": "s390x-linux-gnu-gcc", "qemu": "qemu-s390x", "bits": 64,
              "gmp": "multiarch", "triplet": "s390x-linux-gnu"},
    "ppc":   {"cc": "powerpc-linux-gnu-gcc", "qemu": "qemu-ppc", "bits": 32,
              "gmp": "source", "triplet": "powerpc-linux-gnu"},
}
GMP_URL = "https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz"

# variant -> (big-endian?, DECLITEND, generated dir)
VARIANTS = {
    "le":        (False, 1, "generated_le"),
    "be":        (True,  0, "generated"),
    "be-noswap": (True,  0, "generated_le"),
    "be-litend": (True,  1, "generated"),
}

GTK_STUB = """/* stand-in for GTK in the test-suite build: only the types the C47 headers name */
#pragma once
#include <stdint.h>
typedef struct _GtkWidget GtkWidget;
typedef int gboolean;
typedef void *gpointer;
typedef char gchar;
typedef int gint;
typedef unsigned int guint;
typedef int64_t gint64;
typedef struct { int type; void *window; signed char send_event; unsigned int time;
                 unsigned int state; unsigned int keyval; int length; char *string;
                 unsigned short hardware_keycode; unsigned char group;
                 unsigned int is_modifier : 1; } GdkEventKey;
gint64 g_get_monotonic_time(void);
gint64 g_get_real_time(void);
"""

GLIB_STUB_C = """#include <time.h>
#include <stdint.h>
int64_t g_get_monotonic_time(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
int64_t g_get_real_time(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
  return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
"""


def meson_files(path, var):
    with open(path) as f:
        text = f.read()
    m = re.search(r"^" + re.escape(var) + r"\s*=\s*files\((.*?)\)", text, re.S | re.M)
    if not m:
        sys.exit(f"{path}: no '{var} = files(...)'")
    return [os.path.join(os.path.dirname(path), p) for p in re.findall(r"'([^']+)'", m.group(1))]


def sources(gen):
    s = meson_files(os.path.join(UP, "dep", "meson.build"), "decNumber_src")
    s += meson_files(os.path.join(UP, "src", "c47", "meson.build"), "c47_src")
    s += meson_files(os.path.join(UP, "src", "testSuite", "meson.build"), "testSuite_src")
    s += [os.path.join(ROOT, "build", gen, n)
          for n in ("rasterFontsData.c", "constantPointers.c", "constantPointers2.c")]
    return s


def gmp_from_source(arch, jobs):
    """GMP for a target Ubuntu has no package for: built once into build/betest/gmp-<arch>."""
    tri = ARCHES[arch]["triplet"]
    prefix = os.path.join(OUT, f"gmp-{arch}")
    if os.path.isfile(os.path.join(prefix, "lib", "libgmp.a")):
        return prefix
    src = os.path.join(OUT, "gmp-src")
    os.makedirs(src, exist_ok=True)
    tar = os.path.join(src, os.path.basename(GMP_URL))
    if not os.path.isfile(tar):
        subprocess.run(["curl", "-sSfL", "-o", tar, GMP_URL], check=True)
    subprocess.run(["tar", "xf", tar, "-C", src], check=True)
    top = os.path.join(src, os.path.basename(GMP_URL)[:-len(".tar.xz")])
    bdir = os.path.join(src, f"build-{arch}")
    os.makedirs(bdir, exist_ok=True)
    print(f"[gmp] building GMP for {tri} (a few minutes, once)", flush=True)
    log = open(os.path.join(bdir, "build.log"), "w")
    for cmd in ([os.path.join(top, "configure"), f"--host={tri}", "--disable-shared",
                 "--enable-static", "--disable-assembly", f"--prefix={prefix}"],
                ["make", f"-j{jobs}"], ["make", "install"]):
        subprocess.run(cmd, cwd=bdir, check=True, stdout=log, stderr=subprocess.STDOUT)
    return prefix


def build(variant, arch, opt, jobs):
    be, litend, gen = VARIANTS[variant]
    cc = ARCHES[arch]["cc"] if be else "gcc"
    bits = ARCHES[arch]["bits"] if be else 64
    vdir = os.path.join(OUT, variant if not be else f"{arch}-{variant}")
    obj_dir = os.path.join(vdir, "obj")
    stub = os.path.join(OUT, "stub")
    os.makedirs(os.path.join(stub, "gtk"), exist_ok=True)
    os.makedirs(os.path.join(stub, "gdk"), exist_ok=True)
    os.makedirs(obj_dir, exist_ok=True)
    for p, text in ((os.path.join(stub, "gtk", "gtk.h"), GTK_STUB),
                    (os.path.join(stub, "gdk", "gdk.h"), "#include <gtk/gtk.h>\n"),
                    (os.path.join(stub, "glib_stub.c"), GLIB_STUB_C)):
        with open(p, "w") as f:
            f.write(text)
    flags = [opt, "-g", "-w", "-fsigned-char", "-fno-strict-aliasing",
             "-DPC_BUILD", "-DLINUX", f"-DOS{bits}BIT", "-DTESTSUITE_BUILD", f"-DDECLITEND={litend}",
             "-I" + stub,
             "-I" + os.path.join(ROOT, "build", gen),
             "-I" + os.path.join(UP, "src", "c47"),
             "-I" + os.path.join(UP, "src", "testSuite"),
             "-I" + os.path.join(UP, "dep", "decNumberICU")]
    srcs = sources(gen) + [os.path.join(stub, "glib_stub.c")]
    libs = ["-lgmp", "-lm"]
    if be and ARCHES[arch]["gmp"] == "multiarch":  # the target's GMP (libgmp-dev:<arch>)
        tri = ARCHES[arch]["triplet"]
        flags.insert(0, "-I" + os.path.join(stub, tri))
        os.makedirs(os.path.join(stub, tri), exist_ok=True)
        shutil.copyfile(f"/usr/include/{tri}/gmp.h", os.path.join(stub, tri, "gmp.h"))
        libs = [f"/usr/lib/{tri}/libgmp.a", "-lm"]
    elif be:
        prefix = gmp_from_source(arch, jobs)
        flags.insert(0, "-I" + os.path.join(prefix, "include"))
        libs = [os.path.join(prefix, "lib", "libgmp.a"), "-lm"]
    stamp = " ".join([cc] + flags)

    def one(src):
        obj = os.path.join(obj_dir, os.path.relpath(src, ROOT).replace("/", "__") + ".o")
        r = subprocess.run([cc, "-c", src, "-o", obj] + flags, capture_output=True, text=True)
        return obj, (r.stderr if r.returncode else None)

    t = time.time()
    with ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(one, srcs))
    errs = [(s, e) for s, (_, e) in zip(srcs, res) if e]
    for s, e in errs:
        print(f"--- {s}\n{e}")
    if errs:
        sys.exit(f"{variant}: {len(errs)} file(s) failed to compile")
    exe = os.path.join(vdir, "testSuite")
    r = subprocess.run([cc, "-static", "-o", exe] + [o for o, _ in res] + libs,
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"{variant}: link failed\n{r.stderr}")
    with open(os.path.join(vdir, "flags.txt"), "w") as f:
        f.write(stamp + "\n")
    print(f"[{variant}] built {exe} with {cc} in {time.time() - t:.0f} s", flush=True)
    return exe


def test_files(limit):
    names = []
    with open(LIST) as f:
        for line in f:
            line = line.split(";", 1)[0].strip()
            if line:
                names.append(line)
    return names[:limit] if limit else names


def chunks(names, n):
    """Contiguous chunks of about equal size by line count (the suite's own order kept)."""
    tdir = os.path.dirname(LIST)
    sizes = []
    for nm in names:
        try:
            with open(os.path.join(tdir, nm + ".txt"), "rb") as f:
                sizes.append(f.read().count(b"\n") + 1)
        except OSError:
            sizes.append(1)
    total, out, cur, acc = sum(sizes), [], [], 0
    for nm, sz in zip(names, sizes):
        cur.append(nm)
        acc += sz
        if acc >= total * (len(out) + 1) / n and len(out) < n - 1:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return out


FILE_RE = re.compile(r"^Performing tests from file \S*/(\S+)\.txt ")


def summary_numbers(text):
    tot = re.search(r"NUMBER OF TESTS\s+(\d+)", text)
    ok = re.search(r"(\d+) TESTS?\s+PASSED", text)
    ko = re.search(r"(\d+) TESTS?\s+FAILED", text)
    if not tot:
        return None
    return int(tot.group(1)), int(ok.group(1)), int(ko.group(1))


def run_variant(variant, arch, exe, parts, jobs, timeout):
    be = VARIANTS[variant][0]
    vdir = os.path.join(OUT, variant if not be else f"{arch}-{variant}")
    tdir = os.path.dirname(LIST)

    def one(i):
        wd = os.path.join(vdir, f"run{i:02d}")
        shutil.rmtree(wd, ignore_errors=True)
        os.makedirs(wd)
        os.symlink(os.path.join(UP, "res"), os.path.join(wd, "res"))
        lst = os.path.join(tdir, f"_betest_{os.getpid()}_{arch}_{variant}_{i:02d}.txt")
        with open(lst, "w") as f:
            f.write("\n".join(parts[i]) + "\n")
        cmd = ([ARCHES[arch]["qemu"]] if be else []) + [exe, lst]
        t = time.time()
        try:
            r = subprocess.run(cmd, cwd=wd, capture_output=True, timeout=timeout)
            out, rc = r.stdout.decode("latin-1") + r.stderr.decode("latin-1"), r.returncode
        except subprocess.TimeoutExpired as e:
            out, rc = (e.stdout or b"").decode("latin-1") + "\n*** TIMEOUT ***\n", "timeout"
        finally:
            os.remove(lst)
        with open(os.path.join(vdir, f"chunk{i:02d}.log"), "w") as f:
            f.write(out)
        return i, out, rc, time.time() - t

    t = time.time()
    with ThreadPoolExecutor(jobs) as ex:
        results = list(ex.map(one, range(len(parts))))
    tot = ok = ko = 0
    per_file, bad_chunks = {}, []
    for i, out, rc, dt in results:
        nums = summary_numbers(out)
        if nums is None:
            bad_chunks.append((i, rc, out.strip().split("\n")[-1][:200]))
        else:
            tot, ok, ko = tot + nums[0], ok + nums[1], ko + nums[2]
        per_file.update(failures_per_file(out))
    print(f"[{variant}] ran {len(parts)} chunk(s) in {time.time() - t:.0f} s: "
          f"{tot} tests, {ok} passed, {ko} failed"
          + (f", {len(bad_chunks)} chunk(s) did not finish" if bad_chunks else ""), flush=True)
    for i, rc, last in bad_chunks:
        print(f"    chunk {i} (files {parts[i][0]}..{parts[i][-1]}) exit {rc}: {last}")
    return {"total": tot, "passed": ok, "failed": ko, "per_file": per_file,
            "bad_chunks": bad_chunks}


FAIL_RE = re.compile(r"^in file (\S+)\.txt line (\d+)")


def failures_per_file(out):
    """{file: [(line, first message line), ...]} from the suite's failure reports, which end in
    'in file X.txt line N'; a file a crash cut short gets a ('crash', last output) entry."""
    per, cur, msg = {}, None, []
    for line in out.split("\n"):
        m = FILE_RE.match(line)
        if m:
            cur = m.group(1)
            per[cur] = []
            msg = []
            continue
        f = FAIL_RE.match(line)
        if f and cur:
            first = next((x for x in msg if x.strip() and not x.startswith(("IN:", "OUT:", "-"))), "")
            if (f.group(2), first) not in per[cur]:  # one case can report several times
                per[cur].append((f.group(2), first.strip()))
            msg = []
            continue
        if line.startswith("****"):
            cur = None
        msg.append(line)
    if cur and "NUMBER OF TESTS" not in out:
        per[cur].append(("crash", out.strip().split("\n")[-1][-120:]))
    return per


def ensure_conversions():
    conv = os.path.join(UP, "src", "testSuite", "tests", "conversions.txt")
    if not os.path.isfile(conv):  # sync_upstream.py's git clean removes it; meson's stamp may not know
        os.makedirs(OUT, exist_ok=True)
        subprocess.run([sys.executable, os.path.join(UP, "src", "generateTests", "generateTests.py"),
                        "--stamp", os.path.join(OUT, "conversions_txt.stamp")], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-v", "--variants", default="le,be,be-noswap,be-litend")
    ap.add_argument("--arch", default="s390x", choices=sorted(ARCHES))
    ap.add_argument("--files", type=int, default=0, help="only the first N test files")
    ap.add_argument("--only", default="", help="only these test files (comma-separated names)")
    ap.add_argument("--show", type=int, default=20, help="differing files listed per variant")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--chunks", type=int, default=0, help="default: 4 x jobs")
    ap.add_argument("-O", dest="opt", default="-O2")
    ap.add_argument("--timeout", type=int, default=3600, help="seconds per chunk")
    ap.add_argument("--no-build", action="store_true")
    a = ap.parse_args()
    variants = [v.strip() for v in a.variants.split(",") if v.strip()]
    for v in variants:
        if v not in VARIANTS:
            sys.exit(f"unknown variant {v}; one of {', '.join(VARIANTS)}")
    if not os.path.isdir(UP) or not os.path.isdir(os.path.join(ROOT, "build", "generated")):
        sys.exit("run python3 tools/sync_upstream.py first")
    for tool in {"gcc"} | {ARCHES[a.arch][k] for v in variants if VARIANTS[v][0] for k in ("cc", "qemu")}:
        if not shutil.which(tool):
            sys.exit(f"{tool} not found (see the apt packages at the top of this script)")
    ensure_conversions()

    names = test_files(a.files)
    if a.only:
        want = [x.strip() for x in a.only.split(",") if x.strip()]
        names = [n for n in names if n in want] + [n for n in want if n not in names]
    parts = chunks(names, min(len(names), a.chunks or 4 * a.jobs))
    print(f"{len(names)} test files in {len(parts)} chunks, arch {a.arch}, {a.opt}", flush=True)
    res = {}
    for v in variants:
        vdir = os.path.join(OUT, v if not VARIANTS[v][0] else f"{a.arch}-{v}")
        exe = os.path.join(vdir, "testSuite") if a.no_build else build(v, a.arch, a.opt, a.jobs)
        res[v] = run_variant(v, a.arch, exe, parts, a.jobs, a.timeout)

    print("\n=== summary ===")
    print(f"{'variant':10} {'tests':>7} {'passed':>7} {'failed':>7}  unfinished chunks")
    for v in variants:
        r = res[v]
        print(f"{v:10} {r['total']:7} {r['passed']:7} {r['failed']:7}  {len(r['bad_chunks'])}")
    ref = res.get("le")
    if ref:
        for v in variants:
            if v == "le":
                continue
            diff = [f for f in res[v]["per_file"]
                    if [x[0] for x in res[v]["per_file"][f]]
                    != [x[0] for x in ref["per_file"].get(f, [])]]
            missing = [f for f in ref["per_file"] if f not in res[v]["per_file"]]
            print(f"\n--- {v}: {len(diff)} file(s) whose failures differ from le"
                  + (f", {len(missing)} file(s) not reached" if missing else ""))
            for f in diff[:a.show]:
                fails = res[v]["per_file"][f]
                print(f"  {f}.txt: {len(fails)} failing case(s)")
                for ln, first in fails[:4]:
                    print(f"      line {ln}: {first[:140]}")
            if len(diff) > a.show:
                print(f"  ... and {len(diff) - a.show} more (--show)")
    print(f"\nlogs: {OUT}/<variant>/chunk*.log (big-endian ones in <arch>-<variant>)")
    return 0 if all(r["failed"] == 0 and not r["bad_chunks"] for r in res.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
