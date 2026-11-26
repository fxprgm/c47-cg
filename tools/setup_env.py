#!/usr/bin/env python3
"""Set up a fresh Ubuntu/Debian x86-64 machine (a cloud session) to build and test c47-cg.

    python3 tools/setup_env.py [TOOLCHAIN_DIR]

1. apt packages for the PC side (upstream's generators and the PC test build), if missing.
2. xlsxio_xlsx2csv (upstream's font generator calls it; not packaged), built from source into
   /usr/local, if missing.
3. The CG50 toolchain (fxSDK, gint, fxlibc, sh-elf-gcc), if `sh-elf-gcc` isn't on PATH:
   unpacked into ~/.local from TOOLCHAIN_DIR/cg-toolchain.tar.xz, a prebuilt archive kept in
   the private repo fxprgm/cg-toolchain (default TOOLCHAIN_DIR: ../cg-toolchain, a clone of it
   next to this repo). Without it, build the toolchain with GiteaPC (CLAUDE.md, an hour or more).
4. Prints the versions it ends up with.

Needs root (apt, /usr/local) and network access to the package mirrors, GitHub and gitlab.com.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCAL = os.path.expanduser("~/.local")

APT = ["meson", "ninja-build", "pkg-config", "cmake", "make", "gcc", "git", "xz-utils",
       "libfreetype-dev", "libgmp-dev", "libgtk-3-dev", "libminizip-dev", "libexpat1-dev",
       "python3-pil"]


def run(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, **kw)


def have(cmd):
    return shutil.which(cmd) is not None


def apt():
    missing = [p for p in APT if subprocess.run(["dpkg", "-s", p], capture_output=True).returncode]
    if not missing:
        print("apt: all present")
        return
    env = dict(os.environ, DEBIAN_FRONTEND="noninteractive")
    if subprocess.run(["apt-get", "install", "-y", "-q"] + missing, env=env).returncode:
        run(["apt-get", "update", "-q"], env=env)
        run(["apt-get", "install", "-y", "-q"] + missing, env=env)


def xlsxio():
    if have("xlsxio_xlsx2csv"):
        print("xlsxio_xlsx2csv: present")
        return
    tmp = tempfile.mkdtemp(prefix="xlsxio-")
    try:
        run(["git", "clone", "-q", "--depth", "1", "https://github.com/brechtsanders/xlsxio", tmp + "/src"])
        run(["cmake", "-S", tmp + "/src", "-B", tmp + "/b", "-DBUILD_SHARED=OFF",
             "-DBUILD_DOCUMENTATION=OFF", "-DBUILD_EXAMPLES=OFF", "-DWITH_LIBZIP=OFF",
             "-DCMAKE_INSTALL_PREFIX=/usr/local",
             # xlsxio asks for CMake < 3.5, which CMake 4 refuses unless told this
             "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"], stdout=subprocess.DEVNULL)
        run(["cmake", "--build", tmp + "/b", "-j", str(os.cpu_count() or 2)], stdout=subprocess.DEVNULL)
        run(["cmake", "--install", tmp + "/b"], stdout=subprocess.DEVNULL)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def toolchain(src):
    if have("sh-elf-gcc"):
        print("CG50 toolchain: present")
        return
    archive = os.path.join(src, "cg-toolchain.tar.xz")
    if not os.path.isfile(archive):
        sys.exit(f"no CG50 toolchain and no {archive}: clone fxprgm/cg-toolchain there, "
                 "or build the toolchain with GiteaPC (CLAUDE.md)")
    if os.path.realpath(LOCAL) != "/root/.local":
        print(f"warning: the archive's paths are /root/.local, this is {LOCAL}")
    os.makedirs(LOCAL, exist_ok=True)
    run(["tar", "-xJf", archive, "-C", LOCAL])
    bin_ = os.path.join(LOCAL, "bin")
    if bin_ not in os.environ.get("PATH", "").split(":"):
        print(f"note: add {bin_} to PATH")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(ROOT), "cg-toolchain")
    apt()
    xlsxio()
    toolchain(src)
    env = dict(os.environ, PATH=os.path.join(LOCAL, "bin") + ":" + os.environ.get("PATH", ""))
    for cmd in (["sh-elf-gcc", "--version"], ["fxsdk", "--version"], ["meson", "--version"]):
        r = subprocess.run(cmd, env=env, capture_output=True, text=True)
        print(f"{cmd[0]}: {(r.stdout or r.stderr).splitlines()[0] if (r.stdout or r.stderr) else 'missing'}")


if __name__ == "__main__":
    main()
