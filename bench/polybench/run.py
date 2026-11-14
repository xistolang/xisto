#!/usr/bin/env python3
"""PolyBench/C 4.2.1: the C reference against the ports (xisto, and Rust, Go, Node, Bun when they are there), one
program per kernel and per side, measured as bench/benchmarksgame/langs.py measures (bench/measure.py: wall time,
CPU time, peak RSS; 1 warm-up and RUNS runs, the median of time and CPU, the peak of RSS).
The output that counts is the dump of the arrays (POLYBENCH_DUMP_ARRAYS), which PolyBench writes on stderr: one run
per program with the dump, whose hash must be the C reference's, or the line ends in DIFFERS. The timed runs (one
warm-up, then RUNS) are without the dump, as PolyBench is timed: the dump formats every element of the arrays and
would weigh more than some kernels. The kernel alone (the polybench_timer, -DPOLYBENCH_TIME,
"time" on the ports' command line: one line on stdout) goes in a column of its own, "kern".

Usage: python3 bench/polybench/run.py RUNS [SIDE...] [--dataset NAME] [-- KERNEL...]
  SIDE: c (the reference, compiled from the source), gcc (the same with gcc, informative), xisto (ts/KERNEL.ts),
        node and bun (the same .ts), rust (rust/: a Cargo crate with one binary per kernel), go (go/KERNEL/main.go).
        Default: c xisto. The C reference always runs (for the hash), measured only when c is among the sides.
  NAME: MINI, SMALL, MEDIUM, LARGE (default), EXTRALARGE.
  KERNEL: default, every kernel with a port in ts/.
The C source lives outside the repository (bench/polybench/langs.md): POLYBENCH_SRC, by default
~/workspace/tessarotto/port/polybench. The compiler of the C side: CC (default clang).
One line per (kernel, side), the format of langs.py (python3 bench/placar.py RESULTS c compares xisto with C):
  kernel side program wall cpu rss par kern same|DIFFERS"""
import glob, hashlib, os, platform, shutil, statistics, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "bench"))
import measure

SRC = os.environ.get("POLYBENCH_SRC", os.path.expanduser("~/workspace/tessarotto/port/polybench"))
OUT = os.path.join(HERE, "out")
DATASETS = ["MINI", "SMALL", "MEDIUM", "LARGE", "EXTRALARGE"]
ARM = platform.machine() in ("arm64", "aarch64")
NATIVE = "-mcpu=native" if ARM else "-march=native"  # (as xisto passes XISTO_CPU=native to clang)


def c_source(k):
    """the kernel's .c in the PolyBench tree (linear-algebra/blas/gemm/gemm.c...)"""
    hits = [p for p in glob.glob(f"{SRC}/**/{k}/{k}.c", recursive=True)]
    if not hits:
        sys.exit(f"run.py: no {k}.c under {SRC} (bench/polybench/langs.md says how to get the source)")
    return hits[0]


def build_c(k, ds, cc, label, dump=True):
    exe = f"{OUT}/{label}{'' if dump else '-nodump'}/{k}"
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    src = c_source(k)
    flags = ["-O3", NATIVE]
    # -ffp-contract=off: no fused multiply-add, the same doubles as xisto, Rust and Go; the timer without the
    # cache flush (POLYBENCH_NO_FLUSH_CACHE: a 32 MB array touched before the kernel, which would count in the
    # process's time and memory and the ports do not do)
    subprocess.run([cc, *flags, "-ffp-contract=off", f"-D{ds}_DATASET", *(["-DPOLYBENCH_DUMP_ARRAYS"] if dump else []), "-DPOLYBENCH_TIME",
                    "-DPOLYBENCH_NO_FLUSH_CACHE", f"-I{SRC}/utilities", f"-I{os.path.dirname(src)}",
                    f"{SRC}/utilities/polybench.c", src, "-lm", "-o", exe], check=True)
    return [exe], os.path.basename(src)


def build_xisto(k, ds):
    exe = f"{OUT}/xisto/{k}"
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    env = dict(os.environ, XISTO_CPU="native")
    subprocess.run([f"{ROOT}/build/xisto", "build", "-o", exe, f"{HERE}/ts/{k}.ts"], check=True, env=env,
                   stdout=subprocess.DEVNULL)
    return [exe, ds, "time"], f"{k}.ts"


def build_rust(k, ds):
    crate = f"{HERE}/rust"
    if not os.path.exists(f"{crate}/Cargo.toml"):
        return None
    env = dict(os.environ, RUSTFLAGS="-C target-cpu=native")  # opt-level=3, codegen-units=1 in Cargo.toml
    cargo = shutil.which("cargo") or "cargo"
    if not shutil.which("cargo") and shutil.which("rustup"):  # macOS with rustup: cargo is not on PATH
        cargo = subprocess.run(["rustup", "which", "cargo"], capture_output=True, text=True).stdout.strip() or "cargo"
        env["PATH"] = os.path.dirname(cargo) + os.pathsep + env.get("PATH", "")
    subprocess.run([cargo, "build", "--release", "--quiet", "--manifest-path", f"{crate}/Cargo.toml"], check=True, env=env)
    exe = f"{crate}/target/release/{k}"  # binaries carry the C kernel names (jacobi-2d, floyd-warshall)
    return ([exe, ds, "time"], f"{k}.rs") if os.path.exists(exe) else None


def build_go(k, ds, dump=True):
    mod = f"{HERE}/go"
    if not os.path.exists(f"{mod}/cmd/{k}/main.go"):
        return None
    exe = f"{OUT}/go/{ds}{'' if dump else '-nodump'}/{k}"
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    env = dict(os.environ, GOAMD64="v3") if not ARM else dict(os.environ)
    tg = ([] if ds == "LARGE" else [ds.lower()]) + ([] if dump else ["nodump"])  # sizes and the dump: build tags
    tags = ["-tags", ",".join(tg)] if tg else []
    subprocess.run(["go", "build", *tags, "-o", exe, f"./cmd/{k}"], cwd=mod, check=True, env=env)
    return [exe, ds, "time"], f"{k}.go"


def build(side, k, ds, dump=True):
    """(command, program name) of the side's program for the kernel, or None when the side has no port of it.
    dump=False: the timing build, as PolyBench is timed: C without -DPOLYBENCH_DUMP_ARRAYS, Go with -tags nodump,
    the others with "nodump" on the command line (each still prints the arrays when told to, so the kernel's
    results stay live, like the C's argc > 42 test)"""
    if side == "c":
        return build_c(k, ds, os.environ.get("CC", "clang"), "c", dump)
    if side == "gcc":
        return build_c(k, ds, os.environ.get("GCC", "gcc"), "gcc", dump)
    if side == "go":
        return build_go(k, ds, dump)
    if side == "xisto":
        b = build_xisto(k, ds)
    elif side in ("node", "bun"):
        b = [side, f"{HERE}/ts/{k}.ts", ds, "time"], f"{k}.ts"
    elif side == "rust":
        b = build_rust(k, ds)
    else:
        sys.exit(f"run.py: unknown side {side}")
    if b is None or dump:
        return b
    return b[0] + ["nodump"], b[1]


def timed(cmd):
    """(wall, cpu, rss, hash of stderr, kernel seconds) of one run, None when it failed"""
    t0 = time.perf_counter()
    with tempfile.TemporaryFile() as out:
        try:
            p = measure.start(cmd, stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.PIPE)
        except OSError:
            return None
        h = hashlib.sha256()
        while True:
            b = p.stderr.read(1 << 20)
            if not b:
                break
            h.update(b)
        status, rss, cpu = measure.finish(p)
        wall = time.perf_counter() - t0
        out.seek(0)
        lines = out.read().split()
    try:
        kern = float(lines[-1])
    except (IndexError, ValueError):
        kern = float("nan")
    return (wall, cpu, rss, h.hexdigest(), kern) if status == 0 else None


def main():
    a = sys.argv[1:]
    only = a[a.index("--") + 1:] if "--" in a else []
    a = a[:a.index("--")] if "--" in a else a
    ds = "LARGE"
    if "--dataset" in a:
        i = a.index("--dataset")
        ds = a[i + 1].upper()
        del a[i:i + 2]
    if ds not in DATASETS:
        sys.exit(f"run.py: dataset {ds}? ({', '.join(DATASETS)})")
    runs = int(a[0])
    sides = a[1:] or ["c", "xisto"]
    kernels = only or sorted(os.path.basename(p)[:-3] for p in glob.glob(f"{HERE}/ts/*.ts"))
    os.chdir(ROOT)
    u = os.uname()
    print(f"# {u.sysname} {u.machine}, {os.cpu_count()} cpu; PolyBench {ds}; median of {runs}")
    for k in kernels:
        ref_cmd, _ = build("c", k, ds)
        ref = timed(ref_cmd)
        if ref is None:
            print(f"{k:<14} c      reference failed", flush=True)
            continue
        for side in sides:
            b = build(side, k, ds)
            if b is None:
                print(f"# {k}: no {side} port", flush=True)
                continue
            cmd, name = b
            check = timed(cmd)  # the output, with the dump: its hash against the C reference's
            tcmd, _ = build(side, k, ds, dump=False)
            timed(tcmd)  # warm-up
            rs = [timed(tcmd) for _ in range(runs)]  # timed without the dump, as PolyBench times
            ok = [r for r in rs if r]
            if not ok or check is None:
                print(f"{k:<14} {side:<6} {name:<20} failed", flush=True)
                continue
            same = "same" if check[3] == ref[3] and len(ok) == len(rs) else "DIFFERS"
            w = statistics.median(r[0] for r in ok)
            c = statistics.median(r[1] for r in ok)
            m = max(r[2] for r in ok)
            kt = statistics.median(r[4] for r in ok)
            print(f"{k:<14} {side:<6} {name:<20} {w:8.3f}s {c:8.2f}cpu {m:8.1f}MB  par {c / w:4.1f}  kern {kt:8.3f}s  {same}",
                  flush=True)


main()
