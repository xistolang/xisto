#!/usr/bin/env python3
"""Controller-level versions: @Controller({ path, version: ['1', '2'] }), VERSION_NEUTRAL, a method's own @Version.
Prints one line per request (status, and the body of a 200: the text of a 404 is another gap); test/expected.txt is
the output against Nest 11 on Node.
Usage: scenario.py http://127.0.0.1:3198"""
import json, sys, urllib.request, urllib.error

BASE = sys.argv[1].rstrip("/")
for path in ["/api/v1/multi", "/api/v2/multi", "/api/multi", "/api/v3/multi/only3", "/api/v1/multi/only3",
             "/api/neutral", "/api/v1/neutral", "/api/v2/two", "/api/two"]:
    try:
        r = urllib.request.urlopen(BASE + path)
        status, text = r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        status, text = e.code, e.read().decode()
    print(f"GET {path} {status} {json.dumps(json.loads(text), sort_keys=True) if status == 200 else ''}")
