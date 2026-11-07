#!/usr/bin/env python3
"""The debugger agent seen by an IDE: a DAP client drives --debug binaries and checks what a CLI script cannot
(pause of a busy loop and of an idle server, Ctrl-C, detach, a client that goes away) and that a release binary
carries no agent. Usage: dap_test.py BUILD_DIR (the xisto executable is ../../build/xisto)."""
import os, signal, socket, subprocess, sys, time, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dap_lib import Dap, DapStdio

HERE = os.path.dirname(os.path.abspath(__file__))
XISTO = os.path.join(HERE, "..", "..", "build", "xisto")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "build", "tests", "debug")
os.makedirs(OUT, exist_ok=True)
fails = 0


def build(name, debug=True):
    exe = os.path.join(OUT, name + ("" if debug else "-release"))
    cmd = [XISTO, "build"] + (["--debug"] if debug else []) + ["-o", exe, os.path.join(HERE, name + ".ts")]
    subprocess.run(cmd, check=True)
    return exe


def check(name, cond, detail=""):
    global fails
    if cond:
        print(f"ok: {name}")
    else:
        fails += 1
        print(f"FAIL: {name} {detail}")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def get(url):
    return urllib.request.urlopen(url, timeout=10).read().decode()


busy = build("busy")
d = Dap(busy)
d.start()
time.sleep(0.3)
d.call("pause", {"threadId": 1})
ev = d.event("stopped")
f = d.frames()
check("pause stops a busy loop", ev["body"]["reason"] == "pause" and f[0]["name"] == "(top level)" and 4 <= f[0]["line"] <= 7, f)
d.call("continue", {"threadId": 1})
time.sleep(0.1)
d.proc.send_signal(signal.SIGINT)
time.sleep(0.2)
check("Ctrl-C does not end a debugged program", d.proc.poll() is None)
d.call("disconnect", {"terminateDebuggee": True})
check("disconnect ends the program", d.proc.wait(timeout=10) is not None)

server = build("server")
port = free_port()
d = Dap(server, [str(port)])
src = os.path.join(HERE, "server.ts")
d.start({src: [5]})
for _ in range(100):
    try:
        socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
        break
    except OSError:
        time.sleep(0.05)
d.call("pause", {"threadId": 1})
ev = d.event("stopped")
f = d.frames()
check("pause stops an idle server at listen", ev["body"]["reason"] == "pause" and f[0]["line"] == 11, f)
d.call("continue", {"threadId": 1})
import threading
result = {}
t = threading.Thread(target=lambda: result.setdefault("body", get(f"http://127.0.0.1:{port}/hello?name=isa")))
t.start()
ev = d.event("stopped")
f = d.frames()
check("breakpoint in a handler", ev["body"]["reason"] == "breakpoint" and f[0]["name"] == "hello" and f[0]["line"] == 5 and f[-1]["name"] == "(top level)", f)
d.call("next", {"threadId": 1})
d.event("stopped")
check("next inside the handler", d.frames()[0]["line"] == 6)
d.call("continue", {"threadId": 1})
t.join(10)
check("the request completes after continue", result.get("body") == "hello isa", result)
d.call("disconnect", {"terminateDebuggee": False})
time.sleep(0.1)
check("detached server keeps serving", get(f"http://127.0.0.1:{port}/hello?name=x") == "hello x")
d.proc.terminate()
d.proc.wait(timeout=10)

short = build("short")
d = Dap(short)
d.start({os.path.join(HERE, "short.ts"): [3]})
d.event("stopped")
d.sock.close()  # the IDE crashed
out = d.proc.communicate(timeout=10)[0].decode()
check("a client that goes away releases the program", d.proc.returncode == 0 and "total 3" in out, out)

# variables as an IDE sees them: scopes, expansion, hover, setVariable
vars_exe = build("vars")
d = Dap(vars_exe)
vsrc = os.path.join(HERE, "vars.ts")
d.start({vsrc: [27]})
d.event("stopped")
d.call("next", {"threadId": 1}); d.event("stopped")
f = d.frames()
scopes = d.call("scopes", {"frameId": 0})["body"]["scopes"]
check("scopes: locals and globals", [x["name"] for x in scopes] == ["Locals", "Globals"], scopes)
loc = {v["name"]: v for v in d.call("variables", {"variablesReference": scopes[0]["variablesReference"]})["body"]["variables"]}
check("locals by name", set(loc) == {"n", "s", "i", "count"} and loc["i"]["value"] == "0" and loc["n"]["type"] == "int", loc)
fields = {v["name"]: v for v in d.call("variables", {"variablesReference": loc["s"]["variablesReference"]})["body"]["variables"]}
check("an object expands into its fields", list(fields) == ["name", "points", "color", "parent"] and fields["color"]["value"] == "'green'", fields)
elems = d.call("variables", {"variablesReference": fields["points"]["variablesReference"]})["body"]["variables"]
check("an array expands into its elements", [e["name"] for e in elems] == ["[0]"] and elems[0]["variablesReference"] > 0, elems)
hover = d.call("evaluate", {"expression": "s.points[0].y", "frameId": 0, "context": "hover"})
check("hover on a path", hover["success"] and hover["body"]["result"] == "0", hover)
r = d.call("setVariable", {"variablesReference": loc["s"]["variablesReference"], "name": "name", "value": "'square'"})
check("setVariable on a field", r["success"] and r["body"]["value"] == "'square'", r)
r = d.call("setVariable", {"variablesReference": scopes[0]["variablesReference"], "name": "n", "value": "2"})
check("setVariable on a parameter", r["success"] and r["body"]["value"] == "2", r)
r = d.call("setVariable", {"variablesReference": scopes[0]["variablesReference"], "name": "count", "value": "1"})
check("a const is read-only", not r["success"] and "read-only" in r["message"], r)
glob = {v["name"]: v for v in d.call("variables", {"variablesReference": scopes[1]["variablesReference"]})["body"]["variables"]}
check("globals", "limit" in glob and "ratio" in glob and "shape" not in glob, glob)
d.call("setBreakpoints", {"source": {"path": vsrc}, "breakpoints": []})
d.call("continue", {"threadId": 1})
out = d.proc.communicate(timeout=10)[0].decode()
check("the program runs with the edited values", out.startswith("squareab! "), out)

# exceptions, conditions and logpoints from an IDE (output events, not stdout)
cond_exe = build("cond")
d = Dap(cond_exe)
csrc = os.path.join(HERE, "cond.ts")
d.call("initialize", {"adapterID": "xisto"})
d.call("launch", {})
d.call("setExceptionBreakpoints", {"filters": ["uncaught"]})
d.call("setBreakpoints", {"source": {"path": csrc}, "breakpoints": [{"line": 11, "logMessage": "{it.name}={it.qty}"}, {"line": 11, "condition": "it.qty === 7"}]})
d.call("configurationDone")
ev = d.event("stopped")
logs = [e["body"]["output"] for e in d.events if e.get("event") == "output"]
check("logpoints arrive as output events", logs == ["pen=2\n", "ink=0\n", "pad=7\n"], logs)
check("a condition next to a logpoint on the same line", ev["body"]["reason"] == "breakpoint" and d.frames()[0]["line"] == 11)
d.call("continue", {"threadId": 1})
ev = d.event("stopped")
info = d.call("exceptionInfo", {"threadId": 1})
check("an uncaught runtime error stops with its message", ev["body"]["reason"] == "exception" and info["body"]["description"] == "array index out of bounds", (ev, info))
d.call("disconnect", {"terminateDebuggee": True})
d.proc.wait(timeout=10)

# xisto dap: the adapter as VS Code runs it (launch builds the program; its output arrives as events)
a = DapStdio(XISTO)
caps = a.call("initialize", {"adapterID": "xisto", "linesStartAt1": True, "pathFormat": "path"})["body"]
check("adapter capabilities", caps.get("supportsSetVariable") and any(f["filter"] == "uncaught" for f in caps["exceptionBreakpointFilters"]), caps)
r = a.call("launch", {"program": os.path.join(HERE, "steps.ts"), "cwd": HERE})
check("launch builds and starts the program", r["success"], r)
a.event("initialized")
a.call("setBreakpoints", {"source": {"path": os.path.join(HERE, "steps.ts")}, "breakpoints": [{"line": 10}]})
a.call("setExceptionBreakpoints", {"filters": ["uncaught"]})
a.call("configurationDone")
ev = a.event("stopped")
fr = a.call("stackTrace", {"threadId": 1})["body"]["stackFrames"]
check("adapter: stopped in square, called from sumSquares", [x["name"] for x in fr] == ["square", "sumSquares", "(top level)"] and fr[0]["source"]["path"].endswith("steps.ts"), fr)
sc = a.call("scopes", {"frameId": 0})["body"]["scopes"]
xs = a.call("variables", {"variablesReference": sc[0]["variablesReference"]})["body"]["variables"]
check("adapter: variables", [(v["name"], v["value"]) for v in xs] == [("x", "1")], xs)
a.call("setBreakpoints", {"source": {"path": os.path.join(HERE, "steps.ts")}, "breakpoints": []})
a.call("continue", {"threadId": 1})
ex = a.event("exited")
a.event("terminated")
check("adapter: the program's output reaches the debug console", a.outputs("stdout") == "start\nresult 14\n" and ex["body"]["exitCode"] == 0, (a.outputs("stdout"), ex))
a.call("disconnect", {})
check("adapter exits after disconnect", a.proc.wait(timeout=10) == 0)

bad = os.path.join(OUT, "bad.ts")
open(bad, "w").write("const x: int = 'nope';\n")
a = DapStdio(XISTO)
a.call("initialize", {"adapterID": "xisto"})
r = a.call("launch", {"program": bad})
check("adapter: a compile error fails launch and shows the message", not r["success"] and "must be int, but this is string" in a.outputs("stderr"), (r, a.outputs("stderr")))
a.call("disconnect", {})
a.proc.wait(timeout=10)

# workers: a breakpoint stops one worker, the others park at their next statement (all-stop)
import threading
port = free_port()
d = Dap(server, [str(port)])
d.proc.kill(); d.proc.wait()
env_workers = dict(os.environ, XISTO_WORKERS="2")
class DapEnv(Dap):
    def __init__(self, exe, args, env):
        self.sock, child = socket.socketpair()
        e = dict(env, XISTO_DEBUG_FD=str(child.fileno()))
        self.proc = subprocess.Popen([exe, *args], pass_fds=[child.fileno()], env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        child.close()
        self.seq = 0; self.events = []; self.buf = b""; self.sock.settimeout(10)
d = DapEnv(server, [str(port)], env_workers)
d.start({src: [5]})
for _ in range(100):
    try:
        socket.create_connection(("127.0.0.1", port), timeout=0.2).close(); break
    except OSError:
        time.sleep(0.05)
results = {}
ts = [threading.Thread(target=lambda k=k: results.setdefault(k, get(f"http://127.0.0.1:{port}/hello?name=w{k}"))) for k in range(2)]
for t in ts: t.start()
ev = d.event("stopped")
tid = ev["body"]["threadId"]
time.sleep(0.4)
threads = d.call("threads")["body"]["threads"]
check("workers: threads are listed", len(threads) >= 2 and any(t["id"] == tid for t in threads), threads)
fr = d.call("stackTrace", {"threadId": tid})["body"]["stackFrames"]
check("workers: the stopped worker's stack", fr[0]["name"] == "hello" and fr[0]["line"] == 5, fr)
check("workers: all-stop holds the other request", len(results) == 0, results)
loc = d.call("variables", {"variablesReference": d.call("scopes", {"frameId": fr[0]["id"]})["body"]["scopes"][0]["variablesReference"]})["body"]["variables"]
check("workers: variables of the stopped worker", any(v["name"] == "req" for v in loc), loc)
d.call("continue", {"threadId": tid})
ev2 = d.event("stopped")
d.call("continue", {"threadId": ev2["body"]["threadId"]})
for t in ts: t.join(10)
check("workers: both requests complete", sorted(results.values()) == ["hello w0", "hello w1"], results)
d.call("disconnect", {"terminateDebuggee": True})
d.proc.wait(timeout=10)

# attach over TCP to a program already running, with a token, on 127.0.0.1 only
port = free_port()
dport = free_port()
proc = subprocess.Popen([server, str(port)], env=dict(os.environ, XISTO_DEBUG_PORT=str(dport)), stderr=subprocess.PIPE, stdout=subprocess.DEVNULL, text=True)
line = proc.stderr.readline()
token = line.strip().split("token ")[-1].rstrip(")")
check("attach: the program says where and the token", f"127.0.0.1:{dport}" in line and len(token) == 32, line)
for _ in range(100):
    try:
        socket.create_connection(("127.0.0.1", port), timeout=0.2).close(); break
    except OSError:
        time.sleep(0.05)
check("attach: the program runs without waiting for a debugger", get(f"http://127.0.0.1:{port}/hello?name=a") == "hello a")
lsof = subprocess.run(["lsof", "-nP", "-a", "-p", str(proc.pid), "-iTCP:%d" % dport, "-sTCP:LISTEN"], capture_output=True, text=True).stdout
check("attach: listens on 127.0.0.1 only", "127.0.0.1:%d" % dport in lsof and "*:%d" % dport not in lsof, lsof)

class Tcp(Dap):
    def __init__(self, p):
        self.sock = socket.create_connection(("127.0.0.1", p), timeout=10)
        self.seq = 0; self.events = []; self.buf = b""
t = Tcp(dport)
t.call("initialize", {"adapterID": "xisto"})
r = t.call("attach", {"token": "0" * 32})
check("attach: a wrong token is refused", not r["success"] and "token" in r["message"], r)
check("attach: and the connection is closed", t.read() is None)
t = Tcp(dport)
t.call("initialize", {"adapterID": "xisto"})
r = t.call("setBreakpoints", {"source": {"path": src}, "breakpoints": [{"line": 5}]})
check("attach: nothing works before the token", not r["success"])
t = Tcp(dport)
t.call("initialize", {"adapterID": "xisto"})
r = t.call("attach", {"token": token})
check("attach: the right token is accepted", r["success"], r)
t.call("setBreakpoints", {"source": {"path": src}, "breakpoints": [{"line": 5}]})
t.call("configurationDone")
res = {}
th = threading.Thread(target=lambda: res.setdefault("b", get(f"http://127.0.0.1:{port}/hello?name=tcp")))
th.start()
ev = t.event("stopped")
check("attach: a breakpoint stops the running program", ev["body"]["reason"] == "breakpoint" and t.frames()[0]["name"] == "hello")
t.call("disconnect", {})
th.join(10)
check("attach: disconnect lets it run on (and releases the stop)", res.get("b") == "hello tcp" and proc.poll() is None, res)
check("attach: breakpoints are gone after the session", get(f"http://127.0.0.1:{port}/hello?name=z") == "hello z")

# the adapter attaching, as VS Code does with request: attach
a = DapStdio(XISTO)
a.call("initialize", {"adapterID": "xisto"})
r = a.call("attach", {"port": dport, "token": token})
check("adapter attach", r["success"], r)
a.event("initialized")
a.call("configurationDone")
a.call("pause", {"threadId": 1})
ev = a.event("stopped")
check("adapter attach: pause an idle server", ev["body"]["reason"] == "pause")
a.call("continue", {"threadId": 1})
a.call("disconnect", {})
a.proc.wait(timeout=10)
check("adapter attach: the program survives the IDE", proc.poll() is None and get(f"http://127.0.0.1:{port}/hello?name=y") == "hello y")
proc.terminate()
proc.wait(timeout=10)

# the adapter over the QBE backend
a = DapStdio(XISTO)
a.call("initialize", {"adapterID": "xisto"})
r = a.call("launch", {"program": os.path.join(HERE, "steps.ts"), "backend": "qbe"})
a.event("initialized")
a.call("setBreakpoints", {"source": {"path": os.path.join(HERE, "steps.ts")}, "breakpoints": [{"line": 11}]})
a.call("configurationDone")
a.event("stopped")
sc = a.call("scopes", {"frameId": 0})["body"]["scopes"]
xs = {v["name"]: v["value"] for v in a.call("variables", {"variablesReference": sc[0]["variablesReference"]})["body"]["variables"]}
r = a.call("setVariable", {"variablesReference": sc[0]["variablesReference"], "name": "y", "value": "5"})
check("adapter qbe: variables, read-only", xs == {"x": "1", "y": "1"} and not r["success"] and "QBE" in r["message"], (xs, r))
a.call("disconnect", {})
a.proc.wait(timeout=10)

alone = subprocess.run([short], capture_output=True, text=True, timeout=10)
check("a --debug binary runs alone without a client", alone.returncode == 0 and alone.stdout == "total 3\n", alone)

release = build("short", debug=False)
blob = open(release, "rb").read()
check("a release binary has no agent", b"XISTO_DEBUG_FD" not in blob and b"xs_dbg" not in blob)
env = dict(os.environ, XISTO_DEBUG_FD="0")
r = subprocess.run([release], capture_output=True, text=True, timeout=10, env=env)
check("a release binary ignores XISTO_DEBUG_FD", r.returncode == 0 and r.stdout == "total 3\n", r)

print(f"debug dap: {'FAIL' if fails else 'ok'} ({fails} failed)")
sys.exit(1 if fails else 0)
