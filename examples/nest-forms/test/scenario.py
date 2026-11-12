#!/usr/bin/env python3
"""DTO forms (applyDecorators, ValidateBy, ValidateIf, Transform, Allow), @Request(), ConfigType + forFeature,
@ApiHeader, a guard from applyDecorators on a class, and Nest's 404. Prints one line per request (status and body);
test/expected.txt is the output against Nest 11 on Node.
Usage: scenario.py http://127.0.0.1:3197"""
import json, sys, urllib.request, urllib.error

BASE = sys.argv[1].rstrip("/")

def call(method, path, body=None, headers=None):
    data = None if body is None else body.encode()
    h = dict(headers or {})
    if body is not None:
        h["Content-Type"] = "application/json"
    req = urllib.request.Request(BASE + path, data=data, method=method, headers=h)
    try:
        r = urllib.request.urlopen(req)
        status, text = r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        status, text = e.code, e.read().decode()
    try:
        shown = json.dumps(json.loads(text), sort_keys=True, ensure_ascii=False)
    except ValueError:
        shown = repr(text)
    print(f"{method} {path} {body or ''} {json.dumps(headers or {}, sort_keys=True) if headers else ''}-> {status} {shown}")

for b in ['{}', '{"ip":"1.2.3.4","company":5}', '{"ip":"  ","company":"abc"}', '{"ip":"1234567890123456","company":1.5}',
          '{"ip":"10.0.0.1","company":null}']:
    call("POST", "/api/forms/tablet", b)
for b in ['{}', '{"name":""}', '{"printer":2}', '{"printer":"1"}', '{"ticket":"X","printer":1}', '{"active":"x"}',
          '{"name":"a","printer":0,"ticket":"S","active":3}', '{"ticket":"S","note":"x"}', '{"ticket":"N"}']:
    call("POST", "/api/forms/kiosk", b)
for b in ['{"name":"a","sortOrder":"DESC"}', '{"name":"a","sortOrder":"up"}', '{"name":"a"}']:
    call("POST", "/api/forms/sort", b)
for b in ['{"name":"a","userId":5}', '{"name":"a"}']:
    call("POST", "/api/forms/legacy", b)
call("GET", "/api/forms/settings")
call("GET", "/api/forms/me", headers={"x-user": "ana:t1"})
call("GET", "/api/forms/me", headers={"x-user": "bia"})
call("GET", "/api/forms/me")
call("GET", "/api/forms/item/7", headers={"x-user": "caio"})
call("GET", "/api/local", headers={"x-scope": "local"})
call("GET", "/api/local", headers={"x-scope": "other"})
for k in ["object", "list", "plain"]:
    call("GET", "/api/forms/denied/" + k)
call("GET", "/api/nope")
call("GET", "/api/forms/nope?x=1")
call("POST", "/api/forms/settings", '{}')
