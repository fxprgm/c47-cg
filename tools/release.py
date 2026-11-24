#!/usr/bin/env python3
"""Make a release: build C47.g3a here and publish it as GitHub release vVERSION.

    python tools/release.py 0.2.0            # a release
    python tools/release.py 0.2.0 --pre      # a pre-release

GitHub can't build the add-in in reasonable time (the CG50 toolchain takes over an hour to
build), so it is built here and handed over through the `builds` branch: VERSION/C47.g3a and
VERSION/NOTES.md are committed there, then tag vVERSION is pushed on main, and the workflow
.github/workflows/release.yml attaches the file to the release.

RELEASE_COMMIT_TRAILER, if set, is appended to the commits it makes.

Steps: main must be clean and pushed; the version goes into CMakeLists.txt (committed and
pushed if it changes); tools/sync_upstream.py, a clean fxsdk build-cg; the notes (this repo's
commit, upstream's commit and version, size and SHA-256); the builds branch; the tag.
"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(cmd, cwd=ROOT, capture=False):
    print("+", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, cwd=cwd, check=True, text=True,
                       stdout=subprocess.PIPE if capture else None)
    return r.stdout.strip() if capture else None


def git(*args, cwd=ROOT, capture=True):
    return run(["git", *args], cwd=cwd, capture=capture)


# RELEASE_COMMIT_TRAILER: lines appended to the commits this script makes (e.g. Co-Authored-By)
TRAILER = os.environ.get("RELEASE_COMMIT_TRAILER", "")


def message(text):
    return text + ("\n\n" + TRAILER if TRAILER else "")


def push(*args, cwd=ROOT):
    # negotiate: a push from a shallow clone then sends only what the remote lacks
    run(["git", "-c", "push.negotiate=true", "push", *args], cwd=cwd)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1 or not re.fullmatch(r"\d+\.\d+\.\d+", args[0]):
        sys.exit(__doc__)
    version, pre = args[0], "--pre" in sys.argv
    tag = "v" + version

    if git("status", "--porcelain"):
        sys.exit("the working tree has changes: commit them first")
    if git("rev-parse", "--abbrev-ref", "HEAD") != "main":
        sys.exit("not on main")
    if git("ls-remote", "origin", "refs/tags/" + tag):
        sys.exit(f"{tag} already exists on GitHub")

    # the version in CMakeLists.txt
    cm = os.path.join(ROOT, "CMakeLists.txt")
    text = open(cm).read()
    new = re.sub(r"project\(C47CG VERSION [0-9.]+", f"project(C47CG VERSION {version}", text)
    if new != text:
        open(cm, "w").write(new)
        git("commit", "-q", "-am", message(f"Version {version}"), capture=False)
    push("origin", "main")
    if git("ls-remote", "origin", "refs/heads/main").split()[0] != git("rev-parse", "HEAD"):
        sys.exit("main on GitHub differs from here: pull or push first")

    # a clean build of exactly this commit
    run([sys.executable, os.path.join(ROOT, "tools", "sync_upstream.py")])
    shutil.rmtree(os.path.join(ROOT, "build-cg"), ignore_errors=True)
    run(["fxsdk", "build-cg"])
    g3a = open(os.path.join(ROOT, "C47.g3a"), "rb").read()

    up_commit = git("rev-parse", "HEAD", cwd=os.path.join(ROOT, "upstream"))
    m = re.search(r'#define VERSION1 "([^"]+)"', open(os.path.join(ROOT, "upstream", "src", "c47", "defines.h")).read())
    up_version = m.group(1) if m else "?"
    commit = git("rev-parse", "HEAD")
    notes = f"""C47 (R47 model) for the Casio fx-CG50 and fx-CG50-family calculators.

- c47-cg {tag}, commit {commit[:10]}
- C47 firmware {up_version}, upstream commit {up_commit[:10]} (https://gitlab.com/rpncalculators/c43)
- `C47.g3a`: {len(g3a):,} bytes, SHA-256 `{hashlib.sha256(g3a).hexdigest()}`

Copy C47.g3a to the calculator's storage memory and start C47 from the main menu. Keys:
[docs/KEYS.md](https://github.com/fxprgm/c47-cg/blob/{tag}/docs/KEYS.md).
"""

    # the builds branch, in a throwaway clone
    url = git("remote", "get-url", "origin")
    tmp = tempfile.mkdtemp(prefix="c47-builds-")
    try:
        git("init", "-q", cwd=tmp)
        git("remote", "add", "origin", url, cwd=tmp)
        if git("ls-remote", "origin", "refs/heads/builds", cwd=tmp):
            git("fetch", "-q", "--depth", "1", "origin", "builds", cwd=tmp)
            git("checkout", "-q", "-b", "builds", "FETCH_HEAD", cwd=tmp)
        else:
            git("checkout", "-q", "--orphan", "builds", cwd=tmp)
            with open(os.path.join(tmp, "README.md"), "w") as f:
                f.write("Release builds of C47.g3a, one folder per version; published by "
                        ".github/workflows/release.yml on main. Not a source branch.\n")
        d = os.path.join(tmp, tag)
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, "C47.g3a"), "wb").write(g3a)
        open(os.path.join(d, "NOTES.md"), "w").write(notes)
        if pre:
            open(os.path.join(d, "PRERELEASE"), "w").write("")
        email = git("config", "user.email")
        name = git("config", "user.name")
        git("add", "-A", cwd=tmp)
        git("-c", f"user.email={email}", "-c", f"user.name={name}", "commit", "-q", "-m",
            message(f"{tag}: C47.g3a from {commit[:10]}"), cwd=tmp)
        push("origin", "builds", cwd=tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    git("tag", "-a", tag, "-m", f"C47 for the fx-CG50 {tag}", capture=False)
    push("origin", tag)
    print(f"\n{tag}: pushed; the release appears at https://github.com/fxprgm/c47-cg/releases "
          "once the workflow has run (a minute)")


if __name__ == "__main__":
    main()
