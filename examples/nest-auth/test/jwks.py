#!/usr/bin/env python3
"""Serves test/jwks.json at /certs (the identity provider's key set) and counts how often it is asked.
Usage: jwks.py PORT"""
import http.server, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
KEYS = open(os.path.join(HERE, "jwks.json"), "rb").read()
hits = 0

class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        global hits
        if self.path == "/certs":
            hits += 1
            body = KEYS
        elif self.path == "/hits":
            body = str(hits).encode()
        else:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass

http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
