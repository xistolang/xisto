#!/bin/sh
# HTTP features end to end: fetch options and timeout, setTimeout inside handlers (non-blocking),
# and graceful shutdown on SIGTERM with a request in flight.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.." || exit 1
X=./build/xisto; T=${TMPDIR:-/tmp}/xisto-http-test; mkdir -p $T
$X build -o $T/server tests/http/server.ts || exit 1
$X build -o $T/client tests/http/client.ts || exit 1
$T/server > $T/server.log 2>&1 & SRV=$!
python3 -c "
import socket,time
t0=time.time()
while time.time()-t0<10:
    try: socket.create_connection(('127.0.0.1',8080),timeout=0.2).close(); break
    except OSError: time.sleep(0.01)"
$T/client
# 20 handlers sleeping 300 ms at once: one thread, so they must overlap (well under 20 x 300 ms)
python3 - <<'PY'
import threading, time, urllib.request
res = []
def go():
    res.append(urllib.request.urlopen("http://127.0.0.1:8080/slow?ms=300").read().decode())
t0 = time.time()
ts = [threading.Thread(target=go) for _ in range(20)]
[t.start() for t in ts]; [t.join() for t in ts]
dt = time.time() - t0
print("20 concurrent sleeps of 300 ms:", "overlapped" if dt < 1.5 else "SERIALIZED", "(%d ok)" % sum(r == "slept 300" for r in res))
PY
# node:async_hooks: 30 concurrent requests, each with its own AsyncLocalStorage store across its awaits
python3 - <<'PY'
import threading, urllib.request
res = {}
def go(i):
    res[i] = urllib.request.urlopen("http://127.0.0.1:8080/ctx?id=r%d&ms=%d" % (i, (i * 37) % 200)).read().decode()
ts = [threading.Thread(target=go, args=(i,)) for i in range(30)]
[t.start() for t in ts]; [t.join() for t in ts]
bad = [v for i, v in res.items() if v != "r%d=r%d/r%d" % (i, i, i)]
print("async local storage per request:", "isolated (%d ok)" % len(res) if not bad and len(res) == 30 else "MIXED %s" % bad[:3])
PY
# channels: 5 handlers park on receive(); 5 publishes wake them one each
python3 - <<'PY'
import threading, time, urllib.request
got = []
def waiter():
    got.append(urllib.request.urlopen("http://127.0.0.1:8080/wait", timeout=5).read().decode())
ws = [threading.Thread(target=waiter) for _ in range(5)]
[w.start() for w in ws]
time.sleep(0.3)
for i in range(5):
    urllib.request.urlopen(urllib.request.Request("http://127.0.0.1:8080/publish", data=("m%d" % i).encode(), method="POST"), timeout=5).read()
[w.join() for w in ws]
print("channel delivered:", sorted(got) == ["got m%d" % i for i in range(5)], sorted(got))
PY
# pipelining: three requests in one packet get three responses, in order
python3 - <<'PY'
import socket
s = socket.create_connection(("127.0.0.1", 8080))
req = b"GET /slow?ms=1 HTTP/1.1\r\nHost: x\r\n\r\n" * 3
s.sendall(req)
s.settimeout(3)
data = b""
try:
    while data.count(b"slept 1") < 3:
        chunk = s.recv(65536)
        if not chunk: break
        data += chunk
except socket.timeout:
    pass
print("pipelined: %d responses" % data.count(b"slept 1"))
s.close()
PY
# pipelining with different requests: each handler sees its own headers, query and parameters, also when the first
# one parks on an await and a large request (it grows the input buffer) arrives meanwhile
python3 - <<'PY'
import socket, time
s = socket.create_connection(("127.0.0.1", 8080))
r = lambda name, i, n, ms=0, pad=0: ("GET /who/%s?n=%s&ms=%d HTTP/1.1\r\nHost: x\r\nX-Id: %s\r\n%s\r\n" % (name, n, ms, i, "X-Pad: %s\r\n" % ("p" * pad) if pad else "")).encode()
s.sendall(r("ann", "a1", "1", ms=50) + r("bob", "b2", "2") + r("cy", "c3", "3"))
time.sleep(0.01)
s.sendall(r("dee", "d4", "4", pad=60000))
s.settimeout(3)
data = b""
try:
    while data.count(b";") < 4:
        chunk = s.recv(65536)
        if not chunk: break
        data += chunk
except socket.timeout: pass
got = [l.split(b";")[0].decode() for l in data.split(b"\r\n\r\n")[1:]]
want = ["who=ann id=a1 n=1", "who=bob id=b2 n=2", "who=cy id=c3 n=3", "who=dee id=d4 n=4"]
print("pipelined distinct requests:", "own data" if got[:4] == want else "MIXED %s" % got)
s.close()
PY
# a client leaves while its handler is parked on a channel: the server must not spin
python3 - "$SRV" <<'PY'
import socket, subprocess, sys, time
pid = sys.argv[1]
s = socket.create_connection(("127.0.0.1", 8080))
s.sendall(b"GET /wait HTTP/1.1\r\nHost: x\r\n\r\n")
time.sleep(0.2)
s.close()
def cpu():
    t = subprocess.run(["ps", "-o", "time=", "-p", pid], capture_output=True, text=True).stdout.strip()
    parts = t.split(":"); v = 0.0
    for p in parts: v = v * 60 + float(p)
    return v
c0 = cpu(); time.sleep(1.0); c1 = cpu()
print("parked client left: %s (%.2f s of CPU in 1 s)" % ("idle cpu" if c1 - c0 < 0.2 else "SPINNING", c1 - c0))
PY
# SIGTERM with a request in flight: it completes, new connections are refused, the process exits 0
python3 - "$SRV" <<'PY'
import os, signal, sys, threading, time, urllib.request, socket
pid = int(sys.argv[1]); out = {}
def slow():
    try: out["inflight"] = urllib.request.urlopen("http://127.0.0.1:8080/slow?ms=700").read().decode()
    except Exception as e: out["inflight"] = "error: %s" % e
t = threading.Thread(target=slow); t.start()
time.sleep(0.2)
os.kill(pid, signal.SIGTERM)
time.sleep(0.2)
try:
    socket.create_connection(("127.0.0.1", 8080), timeout=0.5).close(); refused = False
except OSError: refused = True
t.join()
print("in-flight request during SIGTERM:", out["inflight"])
print("new connections after SIGTERM refused:", refused)
PY
wait $SRV; echo "server exit code: $?"
grep -E "shutting down|shutdown complete" $T/server.log
