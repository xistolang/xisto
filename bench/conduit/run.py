#!/usr/bin/env python3
"""Conduit (NestJS RealWorld) under load: the same source built with xisto and run on Node (NestJS), against
the same Postgres. Per endpoint: req/s, p50/p99 latency, server CPU per 1k requests, peak RSS.
Usage: run.py xisto|node [duration=10s] [concurrency=64]
Also: startup (exec to the first 200 on GET /api/tags, median of STARTS=5 fresh starts), RSS at rest (2 s after
the start, before any data) and RSS after the load; per endpoint the server's CPU share during the run.
Cache (Redis, the article list and an article by slug): per endpoint the latency of a cold call (FLUSHALL, then one
request: database + filling the cache; median of COLD=10, FLUSHALL before each), of a warm call (median of 10 in a
row), the load with the cache warm, and the load without a cache (the Redis container stopped: the app turns the
cache off and reads the database every time; it is started again afterwards). REDIS_CONTAINER (xisto-redis).
  xisto: examples/conduit/conduit (xisto build -o conduit src/main.ts)
  node:  examples/conduit/dist/main.js (npm install && npm run build)"""
import json, os, re, signal, subprocess, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.environ.get("CONDUIT_APP", os.path.join(HERE, "..", "..", "examples", "conduit"))
# the database and the cache: Docker containers by default (test/reset.sh); elsewhere, set these commands
RESET_CMD = os.environ.get("RESET_CMD")  # empties the tables and the cache
FLUSH_CMD = os.environ.get("FLUSH_CMD")  # redis FLUSHALL
REDIS_STOP_CMD = os.environ.get("REDIS_STOP_CMD")
REDIS_START_CMD = os.environ.get("REDIS_START_CMD")
which = sys.argv[1]
DUR = sys.argv[2] if len(sys.argv) > 2 else "10s"
CONC = sys.argv[3] if len(sys.argv) > 3 else "64"
URL = "http://127.0.0.1:3000/api"

def reset():
    if RESET_CMD: subprocess.run(RESET_CMD, shell=True, check=True)
    else: subprocess.run(["sh", os.path.join(APP, "test", "reset.sh")], check=True)

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

REDIS = os.environ.get("REDIS_CONTAINER", "xisto-redis")

def flush():
    if FLUSH_CMD: subprocess.run(FLUSH_CMD, shell=True, check=True, stdout=subprocess.DEVNULL)
    else: subprocess.run(["docker", "exec", REDIS, "redis-cli", "FLUSHALL"], check=True, stdout=subprocess.DEVNULL)

def redis(action):  # stop / start the cache
    cmd = REDIS_STOP_CMD if action == "stop" else REDIS_START_CMD
    if cmd: subprocess.run(cmd, shell=True, check=True, stdout=subprocess.DEVNULL)
    else: subprocess.run(["docker", action, REDIS], check=True, stdout=subprocess.DEVNULL)

def one(path, token=None, method="GET", body=None):  # the latency of one request, in ms
    req = urllib.request.Request(URL + path, data=body.encode() if body else None, method=method)
    if body: req.add_header("Content-Type", "application/json")
    if token: req.add_header("Authorization", "Token " + token)
    t0 = time.perf_counter()
    urllib.request.urlopen(req).read()
    return (time.perf_counter() - t0) * 1000

def median(v):
    v = sorted(v)
    return v[len(v) // 2]

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

def wrk(path, token=None, method="GET", body=None, dur=DUR, conc=CONC):  # LOAD=wrk where oha is not installed
    lua = os.path.join(HERE, "..", "out", "conduit-wrk.lua")
    with open(lua, "w") as f:
        f.write(f"wrk.method = {json.dumps(method)}\n")
        if body: f.write(f"wrk.body = {json.dumps(body)}\nwrk.headers['Content-Type'] = 'application/json'\n")
        if token: f.write(f"wrk.headers['Authorization'] = {json.dumps('Token ' + token)}\n")
        f.write("function done(summary, latency, requests)\n"
                "  io.write(string.format('XWRK %d %d %d %f %f\\n', summary.requests, summary.errors.status + summary.errors.connect"
                " + summary.errors.read + summary.errors.write + summary.errors.timeout, summary.duration,"
                " latency:percentile(50) / 1000, latency:percentile(99) / 1000))\nend\n")
    out = subprocess.run(["wrk", "-t", "2", "-c", conc, "-d", dur, "-s", lua, URL + path], capture_output=True, text=True).stdout
    line = [l for l in out.splitlines() if l.startswith("XWRK")][0].split()
    total, bad, us = int(line[1]), int(line[2]), int(line[3])
    return total / (us / 1e6), float(line[4]), float(line[5]), total - bad, total

def oha(path, token=None, method="GET", body=None, dur=DUR, conc=CONC):
    if os.environ.get("LOAD") == "wrk":
        return wrk(path, token, method, body, dur, conc)
    cmd = ["oha", "-z", dur, "-c", conc, "--no-tui", "--output-format", "json", "-m", method]
    if token: cmd += ["-H", "Authorization: Token " + token]
    if body: cmd += ["-d", body, "-T", "application/json"]
    r = json.loads(subprocess.run(cmd + [URL + path], capture_output=True, text=True).stdout)
    s = r["summary"]
    lat = r.get("latencyPercentiles", {})
    ok = sum(v for k, v in r.get("statusCodeDistribution", {}).items() if k.startswith("2"))
    return s["requestsPerSec"], lat.get("p50", 0) * 1000, lat.get("p99", 0) * 1000, ok, sum(r.get("statusCodeDistribution", {}).values())

STARTS = int(os.environ.get("STARTS", "5"))
ups, rests = [], []
for _ in range(STARTS):  # fresh processes, empty database: startup and memory at rest
    reset()
    p0, up = start()
    time.sleep(2)
    rests.append(cpu_rss(p0.pid)[1])
    ups.append(up)
    p0.send_signal(signal.SIGTERM)
    p0.wait()
ups.sort(); rests.sort()
startup, rest_rss = ups[len(ups) // 2], rests[len(rests) // 2]
reset()
proc, _ = start()
jake = post("/users", {"user": {"username": "jake", "email": "jake@conduit.io", "password": "jakejake"}})["user"]["token"]
post("/users", {"user": {"username": "jane", "email": "jane@conduit.io", "password": "janejane"}})
slug = None
for i in range(50):
    a = post("/articles", {"article": {"title": f"Article number {i}", "description": "about things", "body": "x" * 200, "tagList": ["bench", f"t{i % 5}"]}}, jake)
    slug = slug or a["slug"]
c0, _ = cpu_rss(proc.pid)
print(f"== {which} {os.path.basename(os.environ.get('CONDUIT_BIN', ''))}: startup {startup * 1000:.0f} ms (median of {STARTS}), rss at rest {rest_rss:.1f} MB")
COLD = int(os.environ.get("COLD", "10"))
routes = [
    ("list (2 queries, 20 of 50)", "/articles?limit=20", None, "GET", None, DUR, CONC),
    ("article by slug", "/articles/" + slug, None, "GET", None, DUR, CONC),
    ("current user (guard, JWT, query)", "/user", jake, "GET", None, DUR, CONC),
    ("tags", "/tags", None, "GET", None, DUR, CONC),
    ("login (argon2)", "/users/login", None, "POST", json.dumps({"user": {"email": "jake@conduit.io", "password": "jakejake"}}), os.environ.get("LOGIN_DUR", DUR), CONC),
]
CACHED = {"list (2 queries, 20 of 50)", "article by slug"}  # the routes that go through the cache
lat = {}
for name, path, token, method, body, dur, conc in routes:  # cold and warm single calls
    cold = []
    for _ in range(COLD):
        flush()
        cold.append(one(path, token, method, body))
    one(path, token, method, body)
    warm = [one(path, token, method, body) for _ in range(10)]
    lat[name] = (median(cold), median(warm))
    print(f"   {name:<34} cold {median(cold):7.2f} ms  warm {median(warm):7.2f} ms  {'cache' if name in CACHED else 'não usa cache'}")
rows = []
def load(tag):
  global rss
  for name, path, token, method, body, dur, conc in routes:
    if tag == "no-cache" and name not in CACHED:
        continue
    if tag == "cache":
        one(path, token, method, body)  # the entry in the cache before the load
    t0, _ = cpu_rss(proc.pid)
    w0 = time.time()
    rps, p50, p99, ok, total = oha(path, token, method, body, dur, conc)
    wall = time.time() - w0
    t1, rss = cpu_rss(proc.pid)
    cpu_k = (t1 - t0) / max(total, 1) * 1000
    cores = (t1 - t0) / wall  # the server's CPU during the run, in cores
    print(f"   [{tag}] {name:<34} {rps:9.0f} req/s  p50 {p50:7.2f} ms  p99 {p99:7.2f} ms  cpu/1k {cpu_k * 1000:7.1f} ms  cpu {cores:4.2f} cores  rss {rss:6.1f} MB  ok {ok}/{total}")
    rows.append({"endpoint": name, "load": tag, "cold_ms": lat[name][0], "warm_ms": lat[name][1], "rps": rps, "p50": p50, "p99": p99, "cpu_ms_per_1k": cpu_k * 1000, "cores": cores, "rss_mb": rss, "ok": ok, "total": total})
rss = 0.0
load("cache")
redis("stop")  # no cache: the app reads the database
try:
    time.sleep(1)
    load("no-cache")
finally:
    redis("start")
proc.send_signal(signal.SIGTERM)
proc.wait()
out = os.path.join(HERE, "..", "out", f"conduit-{which}.json")
print(f"   rss after the load {rss:.1f} MB")
json.dump({"which": which, "startup_ms": startup * 1000, "rest_rss_mb": rest_rss, "rows": rows}, open(out, "w"), indent=1)
