"""A minimal Debug Adapter Protocol client over the socketpair a --debug binary takes in XISTO_DEBUG_FD: what an
IDE does, for tests/debug/dap_test.py and bench/debug-ab.py."""
import json, os, socket, subprocess


class Dap:
    def __init__(self, exe, args=(), stdout=subprocess.PIPE):
        self.sock, child = socket.socketpair()
        env = dict(os.environ, XISTO_DEBUG_FD=str(child.fileno()))
        self.proc = subprocess.Popen([exe, *args], pass_fds=[child.fileno()], env=env, stdout=stdout, stderr=subprocess.STDOUT)
        child.close()
        self.seq = 0
        self.events = []
        self.buf = b""
        self.sock.settimeout(10)

    def _recv(self):
        return self.sock.recv(65536)

    def _send(self, data):
        self.sock.sendall(data)

    def read(self):
        while b"\r\n\r\n" not in self.buf:
            chunk = self._recv()
            if not chunk:
                return None
            self.buf += chunk
        head, rest = self.buf.split(b"\r\n\r\n", 1)
        n = int(head.split(b":")[1])
        while len(rest) < n:
            chunk = self._recv()
            if not chunk:
                return None
            rest += chunk
        self.buf = rest[n:]
        return json.loads(rest[:n])

    def send(self, command, arguments=None):
        self.seq += 1
        msg = {"seq": self.seq, "type": "request", "command": command}
        if arguments is not None:
            msg["arguments"] = arguments
        body = json.dumps(msg).encode()
        self._send(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        return self.seq

    def closed(self, waiting):
        """the agent went away: say how (exit status, what it printed), so a failure on another machine explains itself"""
        detail = ""
        try:
            rc = self.proc.wait(timeout=5)
            out = self.proc.stdout.read().decode(errors="replace") if self.proc.stdout else ""
            detail = f" (the debuggee exited with {rc}; its output: {out[-2000:]!r})"
        except Exception as e:
            detail = f" (the debuggee did not exit: {e})"
        return RuntimeError(f"connection closed waiting for {waiting}{detail}")

    def call(self, command, arguments=None):
        s = self.send(command, arguments)
        while True:
            m = self.read()
            if m is None:
                raise self.closed(command)
            if m["type"] == "response" and m["request_seq"] == s:
                return m
            self.events.append(m)

    def event(self, name):
        for i, e in enumerate(self.events):
            if e.get("event") == name:
                return self.events.pop(i)
        while True:
            m = self.read()
            if m is None:
                raise self.closed(f"event {name}")
            if m.get("event") == name:
                return m
            self.events.append(m)

    def start(self, breakpoints=None):
        self.call("initialize", {"adapterID": "xisto", "linesStartAt1": True})
        self.call("launch", {})
        for path, lines in (breakpoints or {}).items():
            self.call("setBreakpoints", {"source": {"path": path}, "breakpoints": [{"line": l} for l in lines]})
        self.call("configurationDone")

    def frames(self):
        return self.call("stackTrace", {"threadId": 1})["body"]["stackFrames"]


class DapStdio(Dap):
    """`xisto dap`, the debug adapter an IDE runs: DAP over its stdin and stdout."""

    def __init__(self, xisto):
        self.proc = subprocess.Popen([xisto, "dap"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.seq = 0
        self.events = []
        self.buf = b""

    def _recv(self):
        return os.read(self.proc.stdout.fileno(), 65536)

    def _send(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def outputs(self, category):
        return "".join(e["body"]["output"] for e in self.events if e.get("event") == "output" and e["body"]["category"] == category)
