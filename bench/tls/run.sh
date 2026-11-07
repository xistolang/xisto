#!/bin/sh
# TLS bench: the same HTTPS hello on Xisto (BearSSL), Node (OpenSSL) and Bun (BoringSSL), one process each.
#   keep-alive  : oha, 64 connections, requests over established sessions (record crypto)
#   handshake   : oha --disable-keepalive, 32 connections, one full handshake per request
#   resumption  : python, 300 sequential connections reusing the TLS session ticket/id (session cache)
# Usage: bench/tls/run.sh [DURATION]   (default 10s)
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.." || exit 1
DUR=${1:-10s}
OUT=bench/out; mkdir -p "$OUT" bench/results
stamp=$(date +%Y-%m-%d)
host=$(sysctl -n machdep.cpu.brand_string 2>/dev/null | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
md="bench/results/$stamp-$host-tls.md"
OHA=$(command -v oha) || { echo "oha not found"; exit 1; }
./build/xisto build -o "$OUT/tls-xisto" tests/tls/server.ts || exit 1
URL="https://127.0.0.1:8443/hello?name=isa"

wait_ready() {
    for i in $(seq 1 300); do curl -sk -o /dev/null "$URL" && return 0; sleep 0.05; done; return 1
}
oha_run() { # $1 = extra flags, $2 = conc -> "rps p99 ok"
    $OHA -z "$DUR" -c "$2" $1 --insecure --no-tui --output-format json "$URL" > "$OUT/oha-tls.json"
    python3 -c "
import json; d=json.load(open('$OUT/oha-tls.json')); s=d['summary']; p=d['latencyPercentiles']
print('%.0f %.2f %d' % (s['requestsPerSec'], p['p99']*1000, d['statusCodeDistribution'].get('200',0)))"
}
resume_run() { # sequential: full handshakes vs resumed ones, ms per connection
    python3 - <<'PY'
import socket, ssl, time
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
ctx.maximum_version = ssl.TLSVersion.TLSv1_2   # BearSSL speaks TLS 1.2; keep all three on the same protocol
def one(sess):
    s = ctx.wrap_socket(socket.create_connection(("127.0.0.1", 8443)), session=sess, server_hostname="localhost")
    s.sendall(b"GET /hello?name=isa HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    while s.recv(65536): pass
    r = s.session_reused; out = s.session; s.close(); return out, r
N = 300
t = time.time()
for _ in range(N): one(None)
full = (time.time() - t) * 1000 / N
sess, _ = one(None); reused = 0; t = time.time()
for _ in range(N):
    sess2, r = one(sess); reused += r
resumed = (time.time() - t) * 1000 / N
print("%.3f %.3f %d" % (full, resumed, reused))
PY
}

{
    echo "# TLS bench $stamp"; echo
    echo "Machine: $(sysctl -n machdep.cpu.brand_string), macOS $(sw_vers -productVersion). node $(node --version), bun $(bun --version). EC P-256 self-signed cert (tests/tls), TLS 1.2 for the resumption test."
    echo
    echo "| server | keep-alive req/s | p99 (ms) | RSS (MB) | handshakes/s | p99 (ms) | RSS (MB) | full hs (ms) | resumed hs (ms) | resumed |"
    echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
} > "$md"

for target in xisto node bun; do
    case $target in
        xisto) set -- "$OUT/tls-xisto" ;;
        node) set -- node tests/tls/server.node.ts ;;
        bun) set -- bun tests/tls/server.bun.ts ;;
    esac
    "$@" > "$OUT/tls-$target.log" 2>&1 &
    pid=$!
    wait_ready || { echo "$target did not start"; kill $pid; continue; }
    curl -sk -o /dev/null "$URL"   # warm-up (first-run signature check on macOS)
    set -- $(oha_run "" 64); ka=$1; kap=$2; karss=$(( $(ps -o rss= -p $pid) / 1024 ))
    set -- $(oha_run "--disable-keepalive" 32); hs=$1; hsp=$2; hsrss=$(( $(ps -o rss= -p $pid) / 1024 ))
    set -- $(resume_run); full=$1; res=$2; reused=$3
    kill $pid; wait $pid 2>/dev/null
    echo "| $target | $ka | $kap | $karss | $hs | $hsp | $hsrss | $full | $res | $reused/300 |" >> "$md"
    printf "%-6s keep-alive %7s req/s p99 %5s ms rss %4s MB | handshake %5s/s p99 %6s ms rss %4s MB | seq full %s ms resumed %s ms (%s/300 reused)\n" \
        $target $ka $kap $karss $hs $hsp $hsrss $full $res $reused
    sleep 1
done
echo "-> $md"
