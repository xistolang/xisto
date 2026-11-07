#!/usr/bin/env python3
"""A runner's output (bench/benchmarksgame/run.py, bench/vm.py, bench/vm.ps1) as a Markdown table: time of the three
runtimes, then CPU of the three, then memory of the three, the winner of each group in bold, the source size (gz,
when the runner gives it: the xisto port and the JS original) and xisto vs the best other (by wall time). Usage: python3 bench/table.py RESULTS.txt"""
import re, sys

CELL = r"\s+(?:([\d.]+)s\s+([\d.]+)cpu\s+([\d.]+)MB|(failed))"  # (failed: out of memory on a small machine, say)
ROW = re.compile(r"^(\S+)" + CELL * 3 + r"\s*(.*)$")


def fmt(v, unit):  # (decimal comma: the tables are for Brazilian Portuguese text)
    if unit == "MB":
        t = f"{v:.0f}MB" if v >= 10 else f"{v:.1f}MB"
    else:
        t = f"{v:.2f}s" if v < 10 else f"{v:.1f}s"
    return t.replace(".", ",")


def group(vals, unit):
    ok = [v for v in vals if v is not None]
    best = min(ok) if ok else None
    return ["falhou" if v is None else f"**{fmt(v, unit)}**" if v == best else fmt(v, unit) for v in vals]


src = open(sys.argv[1]).read().splitlines()
head = [l for l in src if l.startswith("#")]
if head:
    print(f"_{head[0].lstrip('# ')}_\n")
rows = [l for l in src if ROW.match(l)]
has_gz = any(re.search(r"gz \d+/\d+", l) for l in rows)
print("| programa | tempo xisto | tempo Node | tempo Bun | CPU xisto | CPU Node | CPU Bun | mem xisto | mem Node | mem Bun |"
      + (" gz xisto | gz JS |" if has_gz else "") + " xisto vs melhor |")
print("|---|" + "---:|" * (11 if has_gz else 9) + "---|")
for line in src:
    m = ROW.match(line)
    if not m:
        continue
    gs = m.groups()[1:13]
    cells = [gs[k:k + 4] for k in (0, 4, 8)]
    wall = [float(c[0]) if c[0] else None for c in cells]
    cpu = [float(c[1]) if c[1] else None for c in cells]
    mem = [float(c[2]) if c[2] else None for c in cells]
    note = m.group(14).split("  ")[0].strip().replace("faster", "mais rápido").replace("SLOWER", "mais lento")
    note = note.replace(".", ",") if note else ""
    if "rápido" in note:
        note = f"**{note}**"
    g = re.search(r"gz (\d+)/(\d+)", line)
    gzc = (f" {g.group(1)} | {g.group(2)} |" if g else " | |") if has_gz else ""
    print(f"| {m.group(1)} | " + " | ".join(group(wall, "s") + group(cpu, "s") + group(mem, "MB")) + f" |{gzc} {note} |")
