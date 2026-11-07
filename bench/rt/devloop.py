#!/usr/bin/env python3
"""The edit-run loop on Conduit: an edit (a string in src/app.controller.ts, a different one each run), then
the time from starting the tool to the first HTTP response. Each tool gets one unmeasured run first, as in
real work (caches warm); the file is restored at the end.

  xisto      xisto run --dev src/main.ts            (compile to native, start)
  bun        bun src/main.ts                        (transpile and JIT)
  tsc+node   tsc -p . && node dist/main.js          (the usual NestJS loop)

Usage: devloop.py [runs=5]. Needs Postgres for Conduit (examples/conduit/setup.sh) and port 3000 free."""
import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APP = os.path.join(ROOT, "examples", "conduit")
XISTO = os.path.join(ROOT, "build", "xisto")
URL = "http://127.0.0.1:3000/api/tags"


def up():
    try:
        with urllib.request.urlopen(URL, timeout=0.2) as r:
            return r.status == 200
    except Exception:
        return False


def once(cmd, before=None):
    if before:
        before()
    t0 = time.perf_counter()
    p = subprocess.Popen(cmd, cwd=APP, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    try:
        while not up():
            if p.poll() is not None:
                raise SystemExit(f"{cmd}: exited with {p.returncode} before answering")
            if time.perf_counter() - t0 > 60:
                raise SystemExit(f"{cmd}: no answer in 60 s")
            time.sleep(0.005)
        return time.perf_counter() - t0
    finally:
        os.killpg(p.pid, signal.SIGTERM)
        p.wait()
        while up():
            time.sleep(0.01)


EDIT = os.path.join(APP, "src", "app.controller.ts")
counter = [0]


def edit():
    counter[0] += 1
    with open(EDIT) as f:
        src = f.read()
    with open(EDIT, "w") as f:
        f.write(re.sub(r'return "Hello World![^"]*";', f'return "Hello World! {os.getpid()}-{counter[0]}";', src))


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    if up():
        raise SystemExit("port 3000 is in use")
    with open(EDIT) as f:
        original = f.read()
    cases = [
        ("xisto --dev", f"'{XISTO}' run --dev src/main.ts"),
        ("bun", "bun src/main.ts"),
        ("tsc+node", "npx tsc -p . && node dist/main.js"),
    ]
    try:
        for name, cmd in cases:
            once(cmd, edit)  # warm-up
            ts = [once(cmd, edit) for _ in range(runs)]
            print(f"{name:12} median {statistics.median(ts):.3f}s  min {min(ts):.3f}s  max {max(ts):.3f}s")
    finally:
        with open(EDIT, "w") as f:
            f.write(original)


main()
