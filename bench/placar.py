#!/usr/bin/env python3
"""The xisto x Rust scoreboard from a bench/benchmarksgame/langs.py run (python3 bench/placar.py RESULTS.txt); also
xisto x C from a bench/polybench/run.py run (python3 bench/placar.py RESULTS.txt c: one program per side).

For each problem and each side, the variant is the fastest one whose output matched, unless a lighter one is close
enough in time (rule agreed on 8 Nov 2026, the same for both sides):
  - up to 2% slower: chosen when it uses at least 10% less memory;
  - from 2% to 5% slower: chosen when it uses at least 25% less memory;
  - more than 5% slower: never.
A variant chosen for its memory is marked with a dagger. Columns: time, CPU and peak memory of each side, the
best of each pair in bold, and the result by wall time (a tie is within 5% either way).
Rust variants that do not compute what the program asks are left out (NOT_THE_SAME): nbody_9 takes 1/sqrt from
rsqrtps, an approximation, and so does nbody_7 (MANIFESTO.md, "A régua da comparação"); *_6t and other instrumented builds as well."""
import sys

NOT_THE_SAME = {"nbody_7", "nbody_9"}  # (both take 1/sqrt from rsqrtps; nbody_5, _6 are SIMD with an exact sqrt and count)
# xisto variants left out by the same rule (Isa, 13/11: the Benchmarks Game results as the problem asks, nothing
# engineered around it): spectralnorm-8 takes 1/d from a float32 division refined to the same double, instead of
# the double division the problem asks for.
XISTO_NOT_THE_SAME = {"spectralnorm-8"}

def pick(vs):
    """vs: [(time, cpu, mem, name)] of the variants whose output matched"""
    fast = min(vs)
    best, by_mem = fast, False
    for v in vs:
        slower = v[0] / fast[0] - 1
        lighter = 1 - v[2] / fast[2] if fast[2] else 0
        if (slower <= 0.02 and lighter >= 0.10) or (0.02 < slower <= 0.05 and lighter >= 0.25):
            if v[2] < best[2]:
                best, by_mem = v, True
    return best, by_mem

def num(x, unit):
    if unit == "MB":
        s = f"{x:.0f} MB" if x >= 10 else f"{x:.1f} MB"
    else:
        s = f"{x:.2f} s" if unit == "s" else f"{x:.2f} u"
    return s.replace(".", ",")

def pair(a, b, unit):
    sa, sb = num(a, unit), num(b, unit)
    if a < b: sa = f"**{sa}**"
    elif b < a: sb = f"**{sb}**"
    return sa, sb

OTHER = sys.argv[2] if len(sys.argv) > 2 else "rust"  # the other side's label (rust, node, bun, c)
OTHER_NAME = "C" if OTHER == "c" else "Rust"  # the column titles (c: PolyBench, bench/polybench/run.py)
rows = {}
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) < 6 or p[1] not in ("xisto", OTHER) or p[-1] != "same":
        continue
    try:
        t, c, m = float(p[3].rstrip("s")), float(p[4].rstrip("cpu")), float(p[5].rstrip("MB"))
    except ValueError:
        continue
    if p[1] == "rust" and (p[2] in NOT_THE_SAME or p[2].endswith("t")):
        continue
    if p[1] == "xisto" and p[2] in XISTO_NOT_THE_SAME:
        continue
    rows.setdefault(p[0], {}).setdefault(p[1], []).append((t, c, m, p[2]))

out = []
for prob, sides in rows.items():
    if "xisto" not in sides or OTHER not in sides:
        continue
    (x, xm), (r, rm) = pick(sides["xisto"]), pick(sides[OTHER])
    ratio = x[0] / r[0]
    res = "ganha" if ratio < 0.95 else "empata" if ratio <= 1.05 else f"perde {ratio:.2f}x".replace(".", ",")
    cells = [*pair(x[0], r[0], "s"), *pair(x[1], r[1], "u"), *pair(x[2], r[2], "MB")]
    out.append((ratio, f"| {prob} | {x[3]}{' †' if xm else ''} | {r[3]}{' †' if rm else ''} | " + " | ".join(cells) + f" | {res} |"))

print(f"| problema | variante xisto | variante {OTHER_NAME} | tempo xisto | tempo {OTHER_NAME} | CPU xisto | CPU {OTHER_NAME} | mem xisto | mem {OTHER_NAME} | resultado |")
print("|---|---|---|---:|---:|---:|---:|---:|---:|---|")
for _, line in sorted(out):
    print(line)
print("\n† escolhida pela memória: até 2% mais lenta com ≥ 10% menos memória, ou até 5% com ≥ 25% menos.")
