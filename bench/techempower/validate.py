#!/usr/bin/env python3
"""TechEmpower response validator: the checks of the TFB toolset (toolset/test_types/plaintext, json and
verifications.py), on raw HTTP/1.1 so that a duplicated header is seen as it is sent.

Per test URL:
  - status 200; Server, Date and Content-Type present; Content-Length or Transfer-Encoding present;
  - Date in RFC 1123 format ('%a, %d %b %Y %H:%M:%S %Z') and different on a second request one second later
    (the TFB waits 3 s: --tfb-sleep); Content-Type matching the TFB regex (json, plaintext);
  - a header sent twice is reported (the TFB's client would join the values and fail the Content-Type regex);
  - plaintext: body 'Hello, World!' (case-insensitive, extra bytes are a warning) and 16 pipelined requests in one
    write answered with 16 responses in order (the TFB's plaintext load uses pipeline depth 16);
  - json: a JSON object whose only key is 'message' = 'Hello, World!' (case-insensitive), Content-Length exact.

Usage: validate.py http://127.0.0.1:9100 [--tests plaintext,json] [--tfb-sleep]
Exit status 0 when every test passes (warnings allowed). Also imported by run.py (validate()).
"""
import json
import re
import socket
import sys
import time
from datetime import datetime
from urllib.parse import urlparse

CT = {
    'json': r'^application/json(; ?charset=(UTF|utf)-8)?$',
    'plaintext': r'^text/plain(; ?charset=(UTF|utf)-8)?$',
}
PATHS = {'plaintext': '/plaintext', 'json': '/json'}


def read_response(f):
    """Reads one HTTP/1.1 response from a socket file: (status, [(name, value)], body)."""
    line = f.readline()
    if not line:
        raise ConnectionError('connection closed')
    parts = line.decode('latin-1').split(' ', 2)
    status = int(parts[1])
    headers = []
    while True:
        h = f.readline().decode('latin-1')
        if h in ('\r\n', '\n', ''):
            break
        k, _, v = h.partition(':')
        headers.append((k.strip(), v.strip()))
    hd = {k.lower(): v for k, v in headers}
    if 'content-length' in hd:
        body = f.read(int(hd['content-length']))
    elif hd.get('transfer-encoding', '').lower() == 'chunked':
        body = b''
        while True:
            n = int(f.readline().strip().split(b';')[0], 16)
            if n == 0:
                f.readline()
                break
            body += f.read(n)
            f.readline()
    else:
        body = f.read()
    return status, headers, body


def request(host, port, path, count=1, accept='*/*'):
    req = ('GET %s HTTP/1.1\r\nHost: %s\r\nAccept: %s\r\nConnection: keep-alive\r\n\r\n' % (path, host, accept)).encode()
    s = socket.create_connection((host, port), timeout=5)
    try:
        s.sendall(req * count)  # count > 1: pipelined, in one write
        f = s.makefile('rb')
        return [read_response(f) for _ in range(count)]
    finally:
        s.close()


def check(test, base, tfb_sleep=False):
    """[(level, message)] for one test; level is 'fail' or 'warn'."""
    u = urlparse(base)
    host, port = u.hostname, u.port or 80
    path = PATHS[test]
    probs = []
    try:
        status, headers, body = request(host, port, path)[0]
    except Exception as e:
        return [('fail', 'no response: %s' % e)]
    hd = {}
    for k, v in headers:
        if k.lower() in hd:
            probs.append(('fail', 'header sent twice: %s (%r and %r)' % (k, hd[k.lower()], v)))
        hd[k.lower()] = v
    if status != 200:
        probs.append(('fail', 'status %d, expected 200' % status))
    for h in ('Server', 'Date', 'Content-Type'):
        if h.lower() not in hd:
            probs.append(('fail', 'required header missing: %s' % h))
    if 'content-length' not in hd and 'transfer-encoding' not in hd:
        probs.append(('fail', 'Content-Length and Transfer-Encoding both missing'))
    if 'content-length' in hd and int(hd['content-length']) != len(body):
        probs.append(('fail', 'Content-Length %s but body has %d bytes' % (hd['content-length'], len(body))))
    date = hd.get('date')
    if date:
        try:
            datetime.strptime(date, '%a, %d %b %Y %H:%M:%S %Z')
        except ValueError:
            probs.append(('warn', 'Date not in RFC 1123 format: %r' % date))
        time.sleep(3 if tfb_sleep else 1.1)
        try:
            date2 = dict((k.lower(), v) for k, v in request(host, port, path)[0][1]).get('date')
            if date2 == date:
                probs.append(('fail', 'cached Date: %r on two requests %s s apart' % (date, 3 if tfb_sleep else 1.1)))
        except Exception as e:
            probs.append(('fail', 'second request failed: %s' % e))
    ct = hd.get('content-type')
    if ct and not re.match(CT[test], ct):
        probs.append(('fail', 'Content-Type %r does not match %s' % (ct, CT[test])))
    if test == 'plaintext':
        b = body.lower()
        if b'hello, world!' not in b:
            probs.append(('fail', "body %r has no 'Hello, World!'" % body[:60]))
        elif len(b) > 13:
            probs.append(('warn', '%d extra bytes in the body' % (len(b) - 13)))
        try:
            rs = request(host, port, path, count=16)
            okp = sum(1 for st, _, bd in rs if st == 200 and bd.lower() == b'hello, world!')
            if okp != 16:
                probs.append(('fail', 'pipelining: %d of 16 responses correct' % okp))
        except Exception as e:
            probs.append(('fail', 'pipelining: %s' % e))
    else:
        try:
            obj = json.loads(body)
            low = {k.lower(): v.lower() for k, v in obj.items()}
            if low.get('message') != 'hello, world!':
                probs.append(('fail', 'message is %r' % low.get('message')))
            if len(low) > 1:
                probs.append(('warn', 'extra keys: %s' % ', '.join(k for k in low if k != 'message')))
        except Exception as e:
            probs.append(('fail', 'not a JSON object: %s (%r)' % (e, body[:60])))
    return probs


def validate(base, tests=('plaintext', 'json'), tfb_sleep=False, out=sys.stdout):
    """True when every test passes; prints one line per problem."""
    ok = True
    for t in tests:
        probs = check(t, base, tfb_sleep)
        fails = [p for p in probs if p[0] == 'fail']
        ok = ok and not fails
        print('%-9s %s %s' % (t, base, 'FAIL' if fails else 'PASS'), file=out)
        for level, msg in probs:
            print('  %s: %s' % (level, msg), file=out)
    return ok


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    tests = ('plaintext', 'json')
    for a in sys.argv[1:]:
        if a.startswith('--tests='):
            tests = tuple(a.split('=', 1)[1].split(','))
    if not args:
        print(__doc__)
        sys.exit(2)
    sys.exit(0 if validate(args[0], tests, '--tfb-sleep' in sys.argv) else 1)
