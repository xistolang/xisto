#!/usr/bin/env python3
"""The basic library under load (docs/roadmap-11.md, block 2): each bench/std/*.ts built with xisto and run on Node and
Bun too, interleaved, RUNS times; median wall time and peak RSS, and the outputs must agree.
Usage: bench/lang/run.py [RUNS] [name ...]"""
import os, re, statistics, subprocess, sys, time
os.environ.setdefault("XISTO_NODE_COMPAT", "1")  # node: imports in the benchmark sources
HERE = os.path.dirname(os.path.abspath(__file__))
X = os.path.join(HERE, "..", "..", "build", "xisto")
RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 5
names = sys.argv[2:] or sorted(f[:-3] for f in os.listdir(HERE) if f.endswith(".ts"))

def timed(cmd):
    # wall time from perf_counter (time(1) prints 10 ms steps); time(1) still gives the peak RSS
    t0 = time.perf_counter()
    r = subprocess.run(["/usr/bin/time", "-l", *cmd], capture_output=True, text=True)
    real = time.perf_counter() - t0
    rss = int(re.search(r"(\d+)\s+maximum resident set size", r.stderr).group(1)) / 1048576
    return real, rss, r.stdout

print(f"{'program':<16}{'xisto':>18}{'node':>18}{'bun':>18}")
for n in names:
    src = os.path.join(HERE, n + ".ts")
    exe = os.path.join(HERE, "..", "..", "build", "std-" + n)
    subprocess.run([X, "build", "-o", exe, src], check=True)
    cmds = {"xisto": [exe], "node": ["node", src], "bun": ["bun", src]}
    res = {k: [] for k in cmds}
    outs = {}
    for _ in range(RUNS):
        for k, c in cmds.items():
            t, m, o = timed(c)
            res[k].append((t, m))
            outs[k] = o
    agree = len(set(outs.values())) == 1
    cells = "".join(f"{statistics.median(t for t, _ in res[k]):>8.3f}s {max(m for _, m in res[k]):>6.1f}MB" for k in cmds)
    print(f"{n:<16}{cells}{'' if agree else '   OUTPUTS DIFFER'}")
