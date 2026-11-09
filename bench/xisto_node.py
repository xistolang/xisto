#!/usr/bin/env python3
"""xisto against Node on this machine, in the same columns as bench/placar.py (time, CPU and peak memory of each
side, the best of each pair in bold): two tables.
  1. the best of each: xisto's fastest program (<problem>.ts or a <problem>-N.ts) against the Node program of the
     Benchmarks Game (bench/benchmarksgame/node/*.js, the one ported);
  2. the same program: <problem>.ts built by xisto against the same file run by Node (node --import
     packages/xisto/register.mjs: the xisto: packages from their TypeScript reference).
Every output is checked against xisto's simple program. Runs are interleaved; medians of RUNS.
Usage: python3 bench/xisto_node.py NODE RUNS [PROBLEM...]   (from the repository root; NODE >= 22.18)"""
import glob, os, statistics, subprocess, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "benchmarksgame"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure  # noqa: E402
import hashlib, time  # noqa: E402

B = "bench/benchmarksgame"
OUT = f"{B}/out"
PROBLEMS = [("spectralnorm", "spectralnorm-node-1.js", "5500", None),
            ("nbody", "nbody-node-8.js", "50000000", None),
            ("fannkuchredux", "fannkuchredux-node-8.js", "12", None),
            ("binarytrees", "binarytrees-node-7.js", "21", None),
            ("mandelbrot", "mandelbrot-node-1.js", "16000", None),
            ("fasta", "fasta-node-1.js", "25000000", None),
            ("knucleotide", "knucleotide-node-8.js", "0", "fasta-25000000.txt"),
            ("revcomp", "revcomp-node-2.js", "0", "fasta-25000000.txt"),
            ("regexredux", "regexredux-node-4.js", "0", "fasta-5000000.txt"),
            ("pidigits", "pidigits-node-3.js", "10000", None)]


def timed(cmd, stdin):
    t0 = time.perf_counter()
    f = open(f"{OUT}/{stdin}", "rb") if stdin else subprocess.DEVNULL
    p = measure.start(cmd, stdin=f, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    h = hashlib.sha256()
    while True:
        b = p.stdout.read(1 << 20)
        if not b:
            break
        h.update(b)
    status, rss, cpu = measure.finish(p)
    wall = time.perf_counter() - t0
    if stdin:
        f.close()
    return (wall, cpu, rss / 1e6 if rss > 1e5 else rss, h.hexdigest()) if status == 0 else None


def num(x, unit):
    if unit == "MB":
        s = f"{x:.0f} MB" if x >= 10 else f"{x:.1f} MB"
    else:
        s = f"{x:.2f} {unit}"
    return s.replace(".", ",")


def pair(a, b, unit):
    sa, sb = num(a, unit), num(b, unit)
    if a < b:
        sa = f"**{sa}**"
    elif b < a:
        sb = f"**{sb}**"
    return sa, sb


def table(title, rows, lx, ln):
    print(f"\n### {title}\n")
    print(f"| problema | {lx} | {ln} | tempo xisto | tempo Node | CPU xisto | CPU Node | mem xisto | mem Node | xisto mais rápido |")
    print("|---|---|---|---:|---:|---:|---:|---:|---:|---:|")
    for prob, x, n in rows:
        if not x or not n:
            print(f"| {prob} | {x[3] if x else 'falhou'} | {n[3] if n else 'falhou'} | | | | | | | |")
            continue
        t, c, m = pair(x[0], n[0], "s"), pair(x[1], n[1], "u"), pair(x[2], n[2], "MB")
        ratio = f"{n[0] / x[0]:.1f}x".replace(".", ",")  # how many times faster xisto is (below 1: slower)
        print(f"| {prob} | {x[3]} | {n[3]} | {t[0]} | {t[1]} | {c[0]} | {c[1]} | {m[0]} | {m[1]} | {ratio} |")


def main():
    node, runs = sys.argv[1], int(sys.argv[2])
    only = sys.argv[3:]
    os.environ["XISTO_NODE_COMPAT"] = "1"
    os.makedirs(OUT, exist_ok=True)
    print(f"# {os.uname().sysname} {os.uname().machine}, {os.cpu_count()} cpu; {node} "
          f"{subprocess.run([node, '--version'], capture_output=True, text=True).stdout.strip()}; median of {runs}")
    best, same = [], []
    for prob, js, arg, stdin in PROBLEMS:
        if only and prob not in only:
            continue
        variants = []
        for src in [f"{B}/{prob}.ts"] + sorted(glob.glob(f"{B}/{prob}-[0-9]*.ts")):
            name = os.path.basename(src)[:-3]
            exe = f"{OUT}/xn-{name}"
            subprocess.run(["build/xisto", "build", "-o", exe, src], check=True, stdout=subprocess.DEVNULL)
            variants.append(("xisto", name, [os.path.abspath(exe), arg]))
        variants.append(("node", js[:-3], [node, f"{B}/node/{js}", arg]))
        variants.append(("node", f"{prob}.ts", [node, "--import", "./packages/xisto/register.mjs", f"{B}/{prob}.ts", arg]))
        for v in variants:
            timed(v[2], stdin)  # warm-up
        res = {v[1]: [] for v in variants}
        for _ in range(runs):  # interleaved
            for label, name, cmd in variants:
                res[name].append(timed(cmd, stdin))
        ref = None
        meds = {}
        for label, name, cmd in variants:
            ok = [r for r in res[name] if r]
            if not ok:
                print(f"{prob:<14} {label:<6} {name:<24} failed", flush=True)
                continue
            ref = ref or ok[0][3]
            ok_same = all(r[3] == ref for r in ok)
            w, c, m = statistics.median(r[0] for r in ok), statistics.median(r[1] for r in ok), max(r[2] for r in ok)
            meds[name] = (w, c, m, name) if ok_same else None
            print(f"{prob:<14} {label:<6} {name:<24} {w:8.3f}s {c:8.2f}cpu {m:8.1f}MB  par {c / w:4.1f}  "
                  f"{'same' if ok_same else 'DIFFERS'}", flush=True)
        xs = [v for k, v in meds.items() if v and not k.startswith(prob + "-node") and not k.endswith(".ts")]
        best.append((prob, min(xs) if xs else None, meds.get(js[:-3])))
        same.append((prob, meds.get(prob), meds.get(f"{prob}.ts")))
    table("O melhor de cada: o programa xisto mais rápido × o programa Node do jogo", best, "xisto", "Node")
    table("O mesmo programa: <problema>.ts compilado pelo xisto × o mesmo arquivo no Node", same, "xisto", "Node")


main()
