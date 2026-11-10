#!/usr/bin/env python3
"""The Nest features example, end to end: versioning, status codes, validation messages, pipes, exception
filters, @Res, uploads, configuration, injection by token, lifecycle hooks, cron, discovery and the helmet
headers. Prints one line per request: method, path, status, the headers that matter and the body.
Run it against the Node build and the xisto build of the same source and compare the outputs.
Usage: scenario.py http://127.0.0.1:3100"""
import json, sys, time, urllib.request, urllib.error

BASE = sys.argv[1].rstrip("/")
HEADERS = ["content-type", "cache-control", "content-disposition", "vary", "x-frame-options", "x-content-type-options",
           "strict-transport-security", "referrer-policy", "content-security-policy", "cross-origin-opener-policy",
           "x-xss-protection", "origin-agent-cluster"]

def call(method, path, body=None, raw=None, ctype=None):
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        ctype = "application/json"
    elif raw is not None:
        data = raw
    req = urllib.request.Request(BASE + path, data=data, method=method)
    if ctype:
        req.add_header("Content-Type", ctype)
    try:
        r = urllib.request.urlopen(req)
        status, hdrs, text = r.status, r.headers, r.read().decode()
    except urllib.error.HTTPError as e:
        status, hdrs, text = e.code, e.headers, e.read().decode()
    shown = []
    for h in HEADERS:
        v = hdrs.get(h)
        if v is not None:
            shown.append(f"{h}={v.lower().replace(' ', '') if h == 'content-type' else v}")
    try:
        b = json.dumps(json.loads(text), sort_keys=True) if text else '""'
    except ValueError:
        b = repr(text)
    print(f"{method} {path} {status} [{'; '.join(shown)}] {b}")

call("GET", "/api/health")
call("GET", "/api/v1/items")
call("POST", "/api/v1/items", {"name": "pen", "qty": 2, "kind": "A", "tags": [{"name": "red"}], "active": True, "when": "2026-11-10"})
call("POST", "/api/v1/items", {"name": "pencil", "kind": "B", "code": "abc"})
call("POST", "/api/v1/items", {})
call("POST", "/api/v1/items", {"name": "", "kind": "C"})
call("POST", "/api/v1/items", {"name": "x" * 25, "kind": "A", "qty": 0})
call("POST", "/api/v1/items", {"name": "ok", "kind": "A", "qty": 101, "when": "2026-13-01"})
call("POST", "/api/v1/items", {"name": "ok", "kind": "B", "code": "ABC"})
call("POST", "/api/v1/items", {"name": "ok", "kind": "A", "tags": [{"name": "toolong"}, {"name": "b"}, {"name": "c"}]})
call("POST", "/api/v1/items", {"name": "ok", "kind": "A", "qty": 2.5})
call("GET", "/api/v1/items")
call("GET", "/api/v1/items?q=pen")
call("GET", "/api/v1/items?q=cil")
call("GET", "/api/v1/items/1")
call("GET", "/api/v1/items/99")
call("GET", "/api/v1/items/abc")
call("DELETE", "/api/v1/items/1")
call("DELETE", "/api/v1/items/1")
call("GET", "/api/v2/items/export/csv")
boundary = "----xistoboundary"
upload = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"note\"\r\n\r\nhello\r\n"
          f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"data.csv\"\r\nContent-Type: text/csv\r\n\r\n"
          f"a,b\n1,2\n\r\n--{boundary}--\r\n").encode()
call("POST", "/api/v1/items/upload", raw=upload, ctype=f"multipart/form-data; boundary={boundary}")
call("GET", "/api/v1/items/teapot")
call("GET", "/api/v1/items/boom")
call("GET", "/api/v1/items/info")
time.sleep(2.2)
call("GET", "/api/v1/items/ticks")
call("GET", "/api/v1/items/names")
