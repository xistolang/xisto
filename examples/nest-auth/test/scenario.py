#!/usr/bin/env python3
"""The auth example, end to end: JWT tokens checked against a JWKS (RS256) and a secret (HS256) through
AuthGuard(["jwt", "hmac"]), @Public() and roles read with Reflector, the throttler's headers and its 429, global
and route interceptors (logging, an envelope around every answer, a header). One line per request: method, path,
status, the headers that matter and the body; then what the logging interceptor recorded.
Run it against the Node build and the xisto build of the same source and compare the outputs.
Usage: scenario.py http://127.0.0.1:3198 http://127.0.0.1:3199"""
import json, os, sys, urllib.request, urllib.error

BASE = sys.argv[1].rstrip("/")
JWKS = sys.argv[2].rstrip("/")
TOKENS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "tokens.json")))
HEADERS = ["content-type", "x-stamp", "retry-after", "x-ratelimit-limit", "x-ratelimit-remaining", "x-ratelimit-reset",
           "x-ratelimit-limit-short", "x-ratelimit-remaining-short", "x-ratelimit-reset-short"]

def call(method, path, token=None, body=None, label=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    if token is not None:
        req.add_header("Authorization", "Bearer " + TOKENS.get(token, token))
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
    print(f"{method} {path} {label or token or '-'} {status} [{'; '.join(shown)}] {b}")

call("GET", "/api/health")
call("GET", "/api/items")
call("GET", "/api/items", "user")
call("GET", "/api/items/me", "user")
call("GET", "/api/items/me", "admin")
call("GET", "/api/items/me", "expired")
call("GET", "/api/items/me", "issuer")
call("GET", "/api/items/me", "forged")
call("GET", "/api/items/me", "unknownKid")
call("GET", "/api/items/me", "blocked")
call("GET", "/api/items/me", "legacy")
call("GET", "/api/items/me", "legacyNoName")
call("GET", "/api/items/me", "legacyBadSecret")
call("GET", "/api/items/me", "not.a.token", label="malformed")
call("GET", "/api/items/text", "user")
call("GET", "/api/items/count", "user")
call("GET", "/api/items/1", "user")
call("GET", "/api/items/9", "user")
call("POST", "/api/items", "legacy", {"name": "pen"})
call("GET", "/api/items/limited", "user")
call("GET", "/api/items/limited", "user")
call("GET", "/api/items/limited", "user")
call("GET", "/api/items/limited", "admin")
call("GET", "/api/admin/report", "user")
call("GET", "/api/admin/report", "admin")
call("GET", "/api/admin/report")
call("GET", "/api/audit")
print("jwks fetched", urllib.request.urlopen(JWKS + "/hits").read().decode(), "time(s)")
