#!/usr/bin/env python3
"""Turn a key script of the PC test build (tests/*.txt, src/plat_host.c) into the C47_KEYS list
of the emulator test (hexbinoct/casio-cg50, emu_go/c47_probe_test.go): each DMCP key code
becomes the CG50 key that sends it (the inverse of keymap in src/plat_gint.c).

    python3 tests/emu_keys.py tests/readme_graph.txt      # prints the C47_KEYS value

`wait MS` becomes wait:N (N x 100 M instructions, about 1.4 s each at the emulator's
70 M/s), `shot`/`shotfull` become shot; menu/off/quit are left out.
"""
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# DMCP key code -> CG50 key name of c47_probe_test.go's c47Keys
CG = {
    1: "X", 2: "log", 3: "ln", 4: "sin", 5: "cos", 6: "tan",
    7: "ab/c", 8: "S<->D", 9: "(", 10: ")", 11: "OPTN", 12: "ALPHA",
    13: "EXE", 14: "x^2", 15: "x10", 16: "(-)", 17: "DEL",
    18: "UP", 19: "7", 20: "8", 21: "9", 22: "/",
    23: "DOWN", 24: "4", 25: "5", 26: "6", 27: "*",
    28: "SHIFT", 29: "1", 30: "2", 31: "3", 32: "-",
    33: "EXIT", 34: "0", 35: ".", 36: "VARS", 37: "+",
    38: "F1", 39: "F2", 40: "F3", 41: "F4", 42: "F5", 43: "F6",
}


def host_names():
    src = open(os.path.join(HERE, "..", "src", "plat_host.c")).read()
    table = src[src.index("names[] = {"):]
    table = table[:table.index("};")]
    return {n: int(c) for n, c in re.findall(r'\{"([^"]+)",\s*(\d+)\}', table)}


def convert(path):
    names = host_names()
    out = []
    for line in open(path):
        line = line.split("#", 1)[0].split()
        if not line:
            continue
        cmd, args = line[0], line[1:]
        if cmd == "key":
            for k in args:
                code = names.get(k)
                if code is None and re.fullmatch(r"k\d+", k):
                    code = int(k[1:])
                if code not in CG:
                    sys.exit(f"{path}: no CG50 key for {k}")
                out.append(CG[code])
        elif cmd == "wait":
            out.append(f"wait:{max(1, math.ceil(int(args[0]) / 1400))}")
        elif cmd in ("shot", "shotfull"):
            out.append("shot")
    return ",".join(out)


if __name__ == "__main__":
    print(convert(sys.argv[1]))
