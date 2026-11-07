#!/usr/bin/env python3
"""Conduit (NestJS RealWorld) under load: the same source built with xisto and run on Node (NestJS), against
the same Postgres. Per endpoint: req/s, p50/p99 latency, server CPU per 1k requests, peak RSS.
Usage: run.py xisto|node [duration=10s] [concurrency=64]
  xisto: examples/conduit/conduit (xisto build -o conduit src/main.ts)
  node:  examples/conduit/dist/main.js (npm install && npm run build)"""
import json, os, re, signal, subprocess, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.join(HERE, "..", "..", "examples", "conduit")
which = sys.argv[1]
DUR = sys.argv[2] if len(sys.argv) > 2 else "10s"
CONC = sys.argv[3] if len(sys.argv) > 3 else "64"
URL = "http://127.0.0.1:3000/api"

def reset():
    subprocess.run(["sh", os.path.join(APP, "test", "reset.sh")], check=True)

def start():
    xbin = os.environ.get("CONDUIT_BIN", os.path.join(APP, "conduit"))  # e.g. a build with or without the cache
    cmd = [xbin] if which == "xisto" else ["node", os.path.join(APP, "dist", "main.js")]
    t0 = time.time()
    p = subprocess.Popen(cmd, cwd=APP, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    while True:
        try:
            urllib.request.urlopen(URL + "/tags", timeout=0.2)
            return p, time.time() - t0
        except Exception:
            if time.time() - t0 > 20: raise SystemExit("server did not start")
            time.sleep(0.01)

def post(path, body, token=None):
    req = urllib.request.Request(URL + path, data=json.dumps(body).encode(), method="POST")
    req.add_header("Content-Type", "application/json")
    if token: req.add_header("Authorization", "Token " + token)
    return json.loads(urllib.request.urlopen(req).read())

def cpu_rss(pid):
    out = subprocess.run(["ps", "-o", "time=,rss=", "-p", str(pid)], capture_output=True, text=True).stdout.split()
    t = out[0].replace("-", ":").split(":")
    secs = sum(float(v) * 60 ** i for i, v in enumerate(reversed(t)))
    return secs, int(out[1]) / 1024

def oha(path, token=None, method="GET", body=None, dur=DUR, conc=CONC):
    cmd = ["oha", "-z", dur, "-c", conc, "--no-tui", "--output-format", "json", "-m", method]
    if token: cmd += ["-H", "Authorization: Token " + token]
    if body: cmd += ["-d", body, "-T", "application/json"]
    r = json.loads(subprocess.run(cmd + [URL + path], capture_output=True, text=True).stdout)
    s = r["summary"]
    lat = r.get("latencyPercentiles", {})
    ok = sum(v for k, v in r.get("statusCodeDistribution", {}).items() if k.startswith("2"))
    return s["requestsPerSec"], lat.get("p50", 0) * 1000, lat.get("p99", 0) * 1000, ok, sum(r.get("statusCodeDistribution", {}).values())

reset()
proc, startup = start()
jake = post("/users", {"user": {"username": "jake", "email": "jake@conduit.io", "password": "jakejake"}})["user"]["token"]
post("/users", {"user": {"username": "jane", "email": "jane@conduit.io", "password": "janejane"}})
slug = None
for i in range(50):
    a = post("/articles", {"article": {"title": f"Article number {i}", "description": "about things", "body": "x" * 200, "tagList": ["bench", f"t{i % 5}"]}}, jake)
    slug = slug or a["slug"]
c0, _ = cpu_rss(proc.pid)
print(f"== {which} {os.path.basename(os.environ.get('CONDUIT_BIN', ''))}: startup {startup * 1000:.0f} ms")
rows = []
for name, path, token, method, body, dur, conc in [
    ("list (2 queries, 20 of 50)", "/articles?limit=20", None, "GET", None, DUR, CONC),
    ("article by slug", "/articles/" + slug, None, "GET", None, DUR, CONC),
    ("current user (guard, JWT, query)", "/user", jake, "GET", None, DUR, CONC),
    ("tags", "/tags", None, "GET", None, DUR, CONC),
    ("login (argon2)", "/users/login", None, "POST", json.dumps({"user": {"email": "jake@conduit.io", "password": "jakejake"}}), "5s", "8"),
]:
    t0, _ = cpu_rss(proc.pid)
    rps, p50, p99, ok, total = oha(path, token, method, body, dur, conc)
    t1, rss = cpu_rss(proc.pid)
    cpu_k = (t1 - t0) / max(total, 1) * 1000
    print(f"   {name:<34} {rps:9.0f} req/s  p50 {p50:7.2f} ms  p99 {p99:7.2f} ms  cpu/1k {cpu_k * 1000:7.1f} ms  rss {rss:6.1f} MB  ok {ok}/{total}")
    rows.append({"endpoint": name, "rps": rps, "p50": p50, "p99": p99, "cpu_ms_per_1k": cpu_k * 1000, "rss_mb": rss, "ok": ok, "total": total})
proc.send_signal(signal.SIGTERM)
proc.wait()
out = os.path.join(HERE, "..", "out", f"conduit-{which}.json")
json.dump({"which": which, "startup_ms": startup * 1000, "rows": rows}, open(out, "w"), indent=1)
