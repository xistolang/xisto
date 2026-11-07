#!/usr/bin/env python3
"""The Benchmarks Game programs (https://benchmarksgame-team.pages.debian.net/benchmarksgame/) on this machine:
the xisto port (bench/benchmarksgame/NAME.ts, the same program with types) against the original Node program
(bench/benchmarksgame/node/*.js) under Node and Bun, at the official size, run interleaved RUNS times; median
wall time, CPU time (user + system), peak RSS (bench/measure.py) and whether xisto's output is the same as Node's.
Bun runs a .cjs copy: some originals assign undeclared names (f = 0), which an ES module refuses.
--trace: when xisto wins or ties (within 3%), one more run of each runtime sampled every second (bench/trace.py:
memory, CPU and, as root with a sensor, energy) into bench/results/trace/DATE-SYSTEM-ARCH/NAME.csv, for charts.
--bg-method: counted as the Benchmarks Game counts (how-programs-are-measured.html): secs the lowest elapsed time
of the runs, cpu secs their mean (RUNS 12 there). Without it, both are medians.
gz, at the end of each line: the source size as the site measures it (comments and repeated whitespace removed,
then gzip at its lowest level), of the xisto port and of the original JS program.
Usage: python3 bench/benchmarksgame/run.py [--trace] [--bg-method] NODE BUN [RUNS] [NAME...]   (from the repository root)"""
import gzip, hashlib, os, re, shutil, statistics, subprocess, sys, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import measure  # bench/measure.py: peak RSS and CPU time, right on Linux too
import trace as tracer  # bench/trace.py: memory, CPU and energy per second

ARGS = [a for a in sys.argv[1:] if a not in ("--trace", "--bg-method")]
TRACE = "--trace" in sys.argv
BG = "--bg-method" in sys.argv  # secs: the lowest; cpu secs: the mean (as the site counts)
WALL = min if BG else statistics.median
CPU = statistics.mean if BG else statistics.median
NODE, BUN = ARGS[0], ARGS[1]
RUNS = int(ARGS[2]) if len(ARGS) > 2 else 3
ONLY = ARGS[3:]
X = os.path.abspath("build/xisto")
B = "bench/benchmarksgame"
OUT = f"{B}/out"
os.makedirs(OUT, exist_ok=True)
os.environ["XISTO_NODE_COMPAT"] = "1"  # import ... from "node:fs"
# name, original, argument, stdin (a file made by an earlier program, or None)
PROGRAMS = [("spectralnorm", "spectralnorm-node-1.js", "5500", None),
            ("nbody", "nbody-node-8.js", "50000000", None),
            ("fannkuchredux", "fannkuchredux-node-8.js", "12", None),
            ("binarytrees", "binarytrees-node-7.js", "21", None),
            ("mandelbrot", "mandelbrot-node-1.js", "16000", None),
            ("fasta", "fasta-node-1.js", "25000000", None),
            ("knucleotide", "knucleotide-node-8.js", "0", "fasta-25000000.txt"),
            ("revcomp", "revcomp-node-2.js", "0", "fasta-25000000.txt"),
            ("regexredux", "regexredux-node-4.js", "0", "fasta-5000000.txt"),
            ("pidigits", "pidigits-node-3.js", "10000", None)]


def input_file(name):
    """the input of the stdin programs: fasta's output at that size (made once by the xisto fasta, which gives
    the same bytes as the original), in bench/benchmarksgame/out"""
    path = f"{OUT}/{name}"
    if not os.path.exists(path):
        n = name.split("-")[1].split(".")[0]
        exe = f"{OUT}/fasta"
        subprocess.run([X, "build", "-o", exe, f"{B}/fasta.ts"], check=True, stdout=subprocess.DEVNULL)
        with open(path + ".tmp", "wb") as f:
            subprocess.run([exe, n], stdout=f, check=True)
        os.rename(path + ".tmp", path)
    return path


def timed(cmd, stdin):
    t0 = time.perf_counter()
    f = open(stdin, "rb") if stdin else subprocess.DEVNULL
    try:
        p = measure.start(cmd, stdin=f, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError:
        return None
    h = hashlib.sha256()  # (a hash, not the output: fasta and revcomp write 250 MB per run)
    while True:
        b = p.stdout.read(1 << 20)
        if not b:
            break
        h.update(b)
    out = h.hexdigest()
    status, rss, cpu = measure.finish(p)
    wall = time.perf_counter() - t0
    if stdin:
        f.close()
    if status != 0:
        return None
    return wall, rss, out, cpu


def cell(rs):
    ok = [r for r in rs if r]
    if not ok:
        return "     failed     "
    return f"{WALL(r[0] for r in ok):8.3f}s {CPU(r[3] for r in ok):7.2f}cpu {max(r[1] for r in ok):6.1f}MB"


def gz(path):
    """the site's source size: comments and repeated whitespace out, then gzip at its lowest level (strings,
    template literals and regex literals are kept as they are, so a // inside one is not taken for a comment)"""
    src, out, i, n = open(path, encoding="utf-8").read(), [], 0, 0
    n = len(src)
    while i < n:
        c = src[i]
        if c in "\"'`":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1]); i = j + 1
        elif src.startswith("//", i):
            j = src.find("\n", i); i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2); i = n if j < 0 else j + 2
            out.append(" ")
        elif c == "/" and re.search(r"[(,=:\[!&|?{};]\s*$", "".join(out[-3:]) or "("):  # a regex literal
            j, cls = i + 1, False
            while j < n and (src[j] != "/" or cls) and src[j] != "\n":
                if src[j] == "\\": j += 1
                elif src[j] == "[": cls = True
                elif src[j] == "]": cls = False
                j += 1
            out.append(src[i:j + 1]); i = j + 1
        else:
            out.append(c); i += 1
    text = re.sub(r"\s+", lambda m: "\n" if "\n" in m.group(0) else " ", "".join(out)).strip()
    return len(gzip.compress(text.encode(), compresslevel=1, mtime=0))


u = os.uname()
ver = lambda c: subprocess.run([c, "--version"], capture_output=True, text=True).stdout.strip()
how = f"secs the lowest, cpu secs the mean of {RUNS} (Benchmarks Game)" if BG else f"median of {RUNS}"
print(f"# {u.sysname} {u.machine}, {os.cpu_count()} cpu; node {ver(NODE)}, bun {ver(BUN)}; {how}")
if TRACE:
    TDIR = f"bench/results/trace/{time.strftime('%Y-%m-%d')}-{u.sysname.lower()}-{u.machine}"
    os.makedirs(TDIR, exist_ok=True)
    IDLE = tracer.idle_watts(1.0)  # None without root or a sensor: no energy columns
print(f"{'program':<15}{'xisto':>27}{'node':>27}{'bun':>27}   xisto vs best other  output")
for name, orig, arg, stdin in PROGRAMS:
    if ONLY and name not in ONLY:
        continue
    exe = f"{OUT}/{name}"
    if stdin:
        stdin = input_file(stdin)
    bp = subprocess.run([X, "build", "-o", exe, f"{B}/{name}.ts"], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if bp.returncode != 0:  # (its first line: on a small machine, often the C compiler killed for memory)
        print(f"{name:<15}  xisto build failed (exit {bp.returncode}): {(bp.stderr.strip().splitlines() or [''])[0][:160]}")
        continue
    cjs = f"{OUT}/{orig[:-3]}.cjs"
    shutil.copyfile(f"{B}/node/{orig}", cjs)
    cmds = {"xisto": [exe, arg], "node": [NODE, f"{B}/node/{orig}", arg], "bun": [BUN, cjs, arg]}
    res = {k: [] for k in cmds}
    for c in cmds.values():  # warm-up, not counted
        timed(c, stdin)
    for _ in range(RUNS):
        for k, c in cmds.items():
            res[k].append(timed(c, stdin))
    med = {k: WALL(r[0] for r in v if r) if any(v) else None for k, v in res.items()}
    others = [m for k, m in med.items() if k != "xisto" and m]
    cmp = ""
    if med["xisto"] and others:
        best = min(others)
        cmp = f"{best / med['xisto']:.2f}x faster" if med["xisto"] <= best else f"{med['xisto'] / best:.2f}x SLOWER"
    outs = {k: {r[2] for r in v if r} for k, v in res.items()}  # every run of every runtime
    failed = [k for k in ("node", "bun") if not outs[k]]  # (out of memory on a small machine, say)
    ref = "node" if outs["node"] else "bun"
    if len(outs["xisto"]) == 1 and outs[ref] and outs["xisto"] <= outs[ref]:
        same = "same" if len(outs[ref]) == 1 else f"same ({ref} itself gave {len(outs[ref])} different outputs)"
        if ref != "node":
            same += " as bun"
        if failed:
            same += f" ({' and '.join(failed)} failed)"
    elif failed and len(failed) == 2:
        same = "node and bun failed"
    else:
        same = "DIFFERS " + " ".join(f"{k}:{len(v)}" for k, v in outs.items())
    print(f"{name:<15}{cell(res['xisto']):>27}{cell(res['node']):>27}{cell(res['bun']):>27}   {cmp:<19}  {same}"
          f"  gz {gz(f'{B}/{name}.ts')}/{gz(f'{B}/node/{orig}')}", flush=True)
    if TRACE and med["xisto"] and others and med["xisto"] <= 1.03 * min(others):
        csv = f"{TDIR}/{name}.csv"
        if os.path.exists(csv):
            os.unlink(csv)
        for k, c in cmds.items():
            tracer.write(csv, k, tracer.trace(k, c, 1.0, stdin, IDLE))
