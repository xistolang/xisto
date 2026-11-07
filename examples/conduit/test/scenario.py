#!/usr/bin/env python3
"""The Conduit API, end to end: every endpoint of the port, with the cases the RealWorld spec cares about
(auth required, validation, not found, duplicates). Prints one line per request: method, path, status and the
JSON body with the parts that differ between runs normalized (tokens, dates, slug suffixes, ids are kept).
Run it against the Node build and the xisto build of the same source and compare the outputs.
Usage: scenario.py http://127.0.0.1:3000/api   (the database must be empty: test/reset.sh)"""
import json, re, sys, urllib.request, urllib.error

BASE = sys.argv[1].rstrip("/")
tokens = {}

def norm(v):
    if isinstance(v, dict):
        return {k: norm(x) for k, x in sorted(v.items())}
    if isinstance(v, list):
        return [norm(x) for x in v]
    if isinstance(v, str):
        if re.fullmatch(r"eyJ[\w-]+\.[\w-]+\.[\w-]+", v): return "<jwt>"
        if re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z", v): return "<date>"
        v = re.sub(r"-[0-9a-z]{1,6}$", "-<rnd>", v) if re.fullmatch(r"[a-z0-9-]+-[0-9a-z]{1,6}", v) else v
    return v

def call(method, path, body=None, user=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method)
    if data is not None: req.add_header("Content-Type", "application/json")
    if user: req.add_header("Authorization", "Token " + tokens[user])
    try:
        r = urllib.request.urlopen(req)
        status, raw = r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        status, raw = e.code, e.read().decode()
    try: shown = json.dumps(norm(json.loads(raw)), sort_keys=True) if raw else '""'
    except ValueError: shown = repr(raw)
    shown_path = re.sub(r"(-[0-9a-z]{1,6})(?=/|$)", "-<rnd>", path) if "/articles/" in path else path
    print(f"{method} {shown_path} {status} {shown}")
    try: return json.loads(raw) if raw else None
    except ValueError: return None

call("GET", "")
call("GET", "/tags")
# users: register, duplicates, validation, login
for u in ("jake", "jane"):
    r = call("POST", "/users", {"user": {"username": u, "email": f"{u}@conduit.io", "password": "jakejake"}})
    tokens[u] = r["user"]["token"]
call("POST", "/users", {"user": {"username": "jake", "email": "jake@conduit.io", "password": "x"}})
call("POST", "/users", {"user": {"username": "", "email": "nobody@conduit.io", "password": "x"}})
call("POST", "/users", {"user": {"username": "bad", "email": "not-an-email", "password": "x"}})
call("POST", "/users", {})
call("POST", "/users/login", {"user": {"email": "jake@conduit.io", "password": "jakejake"}})
call("POST", "/users/login", {"user": {"email": "jake@conduit.io", "password": "wrong"}})
call("POST", "/users/login", {"user": {"email": "", "password": "jakejake"}})
# current user
call("GET", "/user", user="jake")
call("GET", "/user")
call("PUT", "/user", {"user": {"bio": "I like to skateboard", "image": "https://i.stack.imgur.com/xHWG8.jpg"}}, user="jake")
call("GET", "/user", user="jake")
# profiles and follows
call("GET", "/profiles/jane")
call("GET", "/profiles/jane", user="jake")
call("POST", "/profiles/jane/follow", user="jake")
call("GET", "/profiles/jane", user="jake")
call("POST", "/profiles/jane/follow")
# articles
a = call("POST", "/articles", {"article": {"title": "How to train your dragon", "description": "Ever wonder how?", "body": "Very carefully.", "tagList": ["dragons", "training"]}}, user="jane")
slug = a["slug"]
b = call("POST", "/articles", {"article": {"title": "Ação e Reação", "description": "física", "body": "x"}}, user="jake")
call("POST", "/articles", {"article": {"title": "No auth", "description": "d", "body": "b"}})
call("GET", f"/articles/{slug}")
call("GET", "/articles/no-such-article")
call("GET", "/articles")
call("GET", "/articles?tag=dragons")
call("GET", "/articles?author=jake")
call("GET", "/articles?limit=1&offset=1")
call("GET", "/articles/feed", user="jake")
call("GET", "/articles/feed", user="jane")
call("PUT", f"/articles/{slug}", {"article": {"body": "With dragons."}}, user="jane")
# comments
c = call("POST", f"/articles/{slug}/comments", {"comment": {"body": "Thank you so much!"}}, user="jake")
call("GET", f"/articles/{slug}/comments")
cid = c["article"]["comments"][0]["id"]
call("DELETE", f"/articles/{slug}/comments/{cid}", user="jake")
# favorites
call("POST", f"/articles/{slug}/favorite", user="jake")
call("POST", f"/articles/{slug}/favorite", user="jake")
call("GET", "/articles?favorited=jake")
call("DELETE", f"/articles/{slug}/favorite", user="jake")
# unfollow, delete
call("DELETE", "/profiles/jane/follow", user="jake")
call("DELETE", f"/articles/{b['slug']}", user="jake")
call("DELETE", "/users/jane@conduit.io")
call("GET", "/user", user="jane")
