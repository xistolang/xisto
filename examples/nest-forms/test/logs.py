#!/usr/bin/env python3
"""The log lines the controller writes ({"event": ...}), without what changes per run or per runtime: time, pid,
host and pino-http's request (nestjs-pino adds it on Node; xisto's lines do not carry it). Keys keep their order.
Usage: logs.py server.txt"""
import json, sys

DROP = {"time", "pid", "hostname", "req", "@timestamp", "process.pid", "host.hostname"}
for line in open(sys.argv[1], encoding="utf-8"):
    line = line.strip()
    if not line.startswith("{") or '"event"' not in line:
        continue
    d = json.loads(line)
    print(json.dumps({k: v for k, v in d.items() if k not in DROP}, ensure_ascii=False))
