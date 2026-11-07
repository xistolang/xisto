#!/usr/bin/env python3
"""What the debugger costs (docs/debugger.md): each program built as release (-O2), --dev (-O0) and --debug
(-O0 + statement hooks), and run: release, dev, debug alone, debug with a client attached and a breakpoint
armed (on the last top-level statement: hit once at the end, so every statement before it runs armed).
Usage: bench/debug-ab.py [RUNS]"""
import os, statistics, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tests", "debug"))
X = os.path.join(HERE, "..", "build", "xisto")
OUT = os.path.join(HERE, "..", "build", "bench-debug")
os.makedirs(OUT, exist_ok=True)
RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 5
import importlib.util
spec = importlib.util.spec_from_file_location("dap_lib", os.path.join(HERE, "..", "tests", "debug", "dap_lib.py"))
dap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dap)

def build(src, mode):
    exe = os.path.join(OUT, os.path.basename(src)[:-3] + "-" + mode)
    flags = {"release": [], "dev": ["--dev"], "debug": ["--debug"]}[mode]
    subprocess.run([X, "build", *flags, "-o", exe, src], check=True, env=dict(os.environ, XISTO_NODE_COMPAT="1"))
    return exe

def last_top_line(src):
    """the last top-level statement: it runs once, at the end, so the whole run goes armed"""
    last = 1
    for i, l in enumerate(open(src), 1):
        if l[:1] not in ("", " ", "\t", "\n", "}", "/") and not l.startswith(("function", "class", "interface", "import", "enum", "export")):
            last = i
    return last

def run_plain(exe):
    t = time.perf_counter()
    subprocess.run([exe], stdout=subprocess.DEVNULL, check=True)
    return time.perf_counter() - t

def run_armed(exe, src):
    t = time.perf_counter()
    d = dap.Dap(exe, stdout=subprocess.DEVNULL)
    d.start({os.path.abspath(src): [last_top_line(src)]})
    d.event("stopped")
    d.call("continue", {"threadId": 1})
    d.proc.wait()
    return time.perf_counter() - t

progs = [os.path.join(HERE, "programs", f) for f in sorted(os.listdir(os.path.join(HERE, "programs"))) if f.endswith(".ts")]
print(f"{'program':<14}{'release':>10}{'dev':>10}{'debug':>10}{'armed':>10}   debug/dev  armed/dev")
for src in progs:
    exes = {m: build(src, m) for m in ("release", "dev", "debug")}
    times = {k: [] for k in ("release", "dev", "debug", "armed")}
    for _ in range(RUNS):  # interleaved: the four variants share the same machine state
        times["release"].append(run_plain(exes["release"]))
        times["dev"].append(run_plain(exes["dev"]))
        times["debug"].append(run_plain(exes["debug"]))
        times["armed"].append(run_armed(exes["debug"], src))
    med = {k: statistics.median(v) for k, v in times.items()}
    print(f"{os.path.basename(src):<14}" + "".join(f"{med[k]:>9.3f}s" for k in ("release", "dev", "debug", "armed"))
          + f"   {med['debug'] / med['dev']:>8.2f}x  {med['armed'] / med['dev']:>8.2f}x")
