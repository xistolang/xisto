#!/usr/bin/env python3
"""The idiomatic scoreboard from a bench/benchmarksgame/langs.py run (python3 bench/placar_idiomatico.py RESULTS.txt).

For each problem: the program as one writes it first (<problem>.ts, the variant named as the problem) against the
best of the programs written for speed (<problem>-N.ts), on the same machine and run, and against the Rust
scoreboard's pick when the run has Rust. The best -N is chosen as in placar.py (the fastest, or a lighter one close
enough in time). Columns: time, CPU and peak memory; the ratio is by wall time, and the goal (Parte 2) is 1.10x."""
import sys

# (the same choice and formatting as placar.py)
NOT_THE_SAME = {"nbody_7", "nbody_9"}  # (both take 1/sqrt from rsqrtps; nbody_5, _6 are SIMD with an exact sqrt and count)

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


rows = {}
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) < 6 or p[1] not in ("xisto", "rust") or p[-1] != "same":
        continue
    try:
        t, c, m = float(p[3].rstrip("s")), float(p[4].rstrip("cpu")), float(p[5].rstrip("MB"))
    except ValueError:
        continue
    if p[1] == "rust" and (p[2] in NOT_THE_SAME or p[2].endswith("t")):
        continue
    rows.setdefault(p[0], {}).setdefault(p[1], []).append((t, c, m, p[2]))

print("| problema | simples | melhor -N | tempo simples | tempo -N | CPU simples | CPU -N | mem simples | mem -N | razão | meta 1,10x | simples × Rust |")
print("|---|---|---|---:|---:|---:|---:|---:|---:|---:|---|---:|")
inside = total = 0
for prob, sides in rows.items():
    xs = sides.get("xisto", [])
    simple = [v for v in xs if v[3] == prob]
    tuned = [v for v in xs if v[3] != prob]
    if not simple:
        continue
    s = simple[0]
    if tuned:
        b, by_mem = pick(tuned)
        ratio = s[0] / b[0]
        total += 1
        ok = ratio <= 1.10
        inside += ok
        cells = [b[3] + (" †" if by_mem else ""), num(s[0], "s"), num(b[0], "s"), num(s[1], "u"), num(b[1], "u"),
                 num(s[2], "MB"), num(b[2], "MB"), f"{ratio:.2f}x".replace(".", ","), "dentro" if ok else "fora"]
    else:
        cells = ["—", num(s[0], "s"), "—", num(s[1], "u"), "—", num(s[2], "MB"), "—", "—", "sem -N"]
    rust = f"{s[0] / pick(sides['rust'])[0][0]:.2f}x".replace(".", ",") if sides.get("rust") else "—"
    print(f"| {prob} | {prob}.ts | " + " | ".join(cells) + f" | {rust} |")
print(f"\nDentro da meta: {inside} de {total} problemas com -N. † escolhida pela memória (regra do placar.py).")
