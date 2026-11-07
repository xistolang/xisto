#!/usr/bin/env python3
"""xisto, Node and Bun on this machine: every program of bench/programs, bench/rt, bench/lang and bench/std,
built with build/xisto (release), run interleaved RUNS times; median wall time, CPU time (user + system)
and peak RSS of each process
(bench/measure.py), and whether the outputs agree. To compare systems and processors (bench/out/vm-*.txt per machine).
Usage: python3 bench/vm.py NODE BUN [RUNS]   (from the repository root; Python 3.9+, Linux or macOS)"""
import os, statistics, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure  # bench/measure.py: peak RSS and CPU time, right on Linux too

NODE, BUN = sys.argv[1], sys.argv[2]
RUNS = int(sys.argv[3]) if len(sys.argv) > 3 else 3
X = os.path.abspath("build/xisto")
OUT = "bench/out/vm"
os.makedirs(OUT, exist_ok=True)
os.environ["XISTO_NODE_COMPAT"] = "1"  # node: imports in the std benchmarks
GROUPS = [("bench/programs", ["fib", "mandelbrot", "nbody", "objects", "sieve"]),
          ("bench/rt", ["binarytrees", "cycles"]),
          ("bench/lang", ["accessors", "closures", "generators", "generics", "shapes", "unions"]),
          ("bench/std", ["bytes", "date", "promise", "redos", "regex"])]


def timed(cmd):
    """wall seconds, peak RSS in MB, stdout, CPU seconds; None when it failed (out of memory on a small machine, say)"""
    t0 = time.perf_counter()
    try:
        p = measure.start(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError:
        return None
    out = p.stdout.read()
    status, rss, cpu = measure.finish(p)
    wall = time.perf_counter() - t0
    if status != 0:
        return None
    return wall, rss, out, cpu


def cell(rs):
    ok = [r for r in rs if r]
    if not ok:
        return "     failed     "
    return f"{statistics.median(r[0] for r in ok):8.3f}s {statistics.median(r[3] for r in ok):7.2f}cpu {max(r[1] for r in ok):6.1f}MB"


u = os.uname()
print(f"# {u.sysname} {u.machine}, {os.cpu_count()} cpu; node {subprocess.run([NODE, '--version'], capture_output=True, text=True).stdout.strip()}, "
      f"bun {subprocess.run([BUN, '--version'], capture_output=True, text=True).stdout.strip()}; median of {RUNS}")
print(f"{'program':<14}{'xisto':>27}{'node':>27}{'bun':>27}   xisto vs best other")
for d, names in GROUPS:
    for n in names:
        src = f"{d}/{n}.ts"
        exe = f"{OUT}/{n}"
        if subprocess.run([X, "build", "-o", exe, src], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
            print(f"{n:<14}  xisto build failed")
            continue
        cmds = {"xisto": [exe], "node": [NODE, src], "bun": [BUN, src]}
        res = {k: [] for k in cmds}
        for c in cmds.values():  # warm-up, not counted: a new binary's first exec (macOS checks it), disk caches
            timed(c)
        for _ in range(RUNS):
            for k, c in cmds.items():
                res[k].append(timed(c))
        outs = {k: r[0][2] for k, r in res.items() if r and r[0]}
        agree = len(set(outs.values())) <= 1
        med = {k: statistics.median(x[0] for x in r if x) for k, r in res.items() if any(r)}
        others = [med[k] for k in ("node", "bun") if k in med]
        ratio = f"{min(others) / med['xisto']:.2f}x faster" if "xisto" in med and others and med["xisto"] <= min(others) else \
                (f"{med['xisto'] / min(others):.2f}x SLOWER" if "xisto" in med and others else "")
        print(f"{n:<14}{cell(res['xisto']):>27}{cell(res['node']):>27}{cell(res['bun']):>27}   {ratio}{'' if agree else '  OUTPUTS DIFFER'}", flush=True)
