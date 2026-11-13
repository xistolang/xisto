#!/usr/bin/env python3
"""The Benchmarks Game programs of other languages (Rust, Go, C++, Java, C#, Swift, Python...) on this machine,
against the xisto port: the official programs, built as the site builds them (bench/benchmarksgame/langs.md),
measured as bench/benchmarksgame/run.py measures (bench/measure.py: wall time, CPU time, peak RSS), each output
checked against xisto's (which is checked against Node's by run.py).
Usage: python3 bench/benchmarksgame/langs.py RUNS NAME=COMMAND... [-- PROGRAM...]
  NAME=COMMAND: a label and a command with {p} for the problem (spectralnorm...) and {v} for each variant file
  found by the glob in the label's directory; simpler: LABEL=DIR, every executable DIR/{p}* is a variant.
  LABEL=js:RUNTIME (node, bun): the official Node programs bench/benchmarksgame/node/{p}-node-*.js under RUNTIME.
One line per variant: problem, variant, median wall, median CPU, peak RSS, output same/differs."""
import glob, hashlib, os, shutil, statistics, subprocess, sys, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import measure

B = "bench/benchmarksgame"
OUT = f"{B}/out"
# problem, argument, stdin file
PROBLEMS = [("spectralnorm", "5500", None), ("nbody", "50000000", None), ("fannkuchredux", "12", None),
            ("binarytrees", "21", None), ("mandelbrot", "16000", None), ("fasta", "25000000", None),
            ("knucleotide", "0", "fasta-25000000.txt"), ("revcomp", "0", "fasta-25000000.txt"),
            ("regexredux", "0", "fasta-5000000.txt"), ("pidigits", "10000", None)]


def timed(cmd, stdin):
    t0 = time.perf_counter()
    f = open(f"{OUT}/{stdin}", "rb") if stdin else subprocess.DEVNULL
    try:
        p = measure.start(cmd, stdin=f, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError:
        return None
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
    return (wall, cpu, rss, h.hexdigest()) if status == 0 else None


def main():
    a = sys.argv[1:]
    runs = int(a[0])
    only = a[a.index("--") + 1:] if "--" in a else []
    groups = [x.split("=", 1) for x in (a[1:a.index("--")] if "--" in a else a[1:])]
    env = dict(os.environ, XISTO_NODE_COMPAT="1")
    os.environ.update(env)
    print(f"# {os.uname().sysname} {os.uname().machine}, {os.cpu_count()} cpu; median of {runs}")
    for p, arg, stdin in PROBLEMS:
        if only and p not in only:
            continue
        exe = f"{OUT}/{p}"
        subprocess.run(["build/xisto", "build", "-o", exe, f"{B}/{p}.ts"], check=True, stdout=subprocess.DEVNULL)
        variants = [("xisto", p, [os.path.abspath(exe), arg])]
        for v in sorted(glob.glob(f"{B}/{p}-[0-9]*.ts")):  # the programs written for xisto (#2...)
            vexe = f"{OUT}/{os.path.basename(v)[:-3]}"
            subprocess.run(["build/xisto", "build", "-o", vexe, v], check=True, stdout=subprocess.DEVNULL)
            variants.append(("xisto", os.path.basename(v)[:-3], [os.path.abspath(vexe), arg]))
        for label, d in groups:
            if d.startswith("js:"):  # a JS runtime: the official Node programs in bench/benchmarksgame/node
                for v in sorted(glob.glob(f"{B}/node/{p}-node-*.js")):
                    src = v
                    if os.path.basename(d[3:]).startswith("bun"):  # Bun runs a .cjs copy (as run.py)
                        src = f"{OUT}/{os.path.basename(v)[:-3]}.cjs"
                        shutil.copyfile(v, src)
                    variants.append((label, os.path.basename(v)[:-3], [d[3:], src, arg]))
                continue
            for v in sorted(glob.glob(f"{d}/{p}") + glob.glob(f"{d}/{p}_*")):
                if os.access(v, os.X_OK) and not v.endswith((".d", ".rlib")):
                    variants.append((label, os.path.basename(v), [v, arg]))
        ref = None
        for label, name, cmd in variants:
            timed(cmd, stdin)  # warm-up
            rs = [timed(cmd, stdin) for _ in range(runs)]
            ok = [r for r in rs if r]
            if not ok:
                print(f"{p:<14} {label:<6} {name:<20} failed", flush=True)
                continue
            if ref is None:
                ref = ok[0][3]
            same = "same" if all(r[3] == ref for r in ok) else "DIFFERS"
            w = statistics.median(r[0] for r in ok)
            c = statistics.median(r[1] for r in ok)
            m = max(r[2] for r in ok)
            print(f"{p:<14} {label:<6} {name:<20} {w:8.3f}s {c:8.2f}cpu {m:8.1f}MB  par {c / w:4.1f}  {same}", flush=True)


main()
