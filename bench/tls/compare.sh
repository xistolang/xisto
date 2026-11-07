#!/bin/sh
# TLS backends side by side: Xisto with BearSSL (patched, TLS 1.2), Xisto with AWS-LC (TLS 1.3), Node, Bun.
# Needs XISTO_AWSLC pointing at an AWS-LC install (include/, lib/). Usage: XISTO_AWSLC=... sh bench/tls/compare.sh [DURATION]
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.." || exit 1
DUR=${1:-5s}
O=bench/out; mkdir -p $O bench/results
URL="https://127.0.0.1:8443/hello?name=isa"
stamp=$(date +%Y-%m-%d); host=$(sysctl -n machdep.cpu.brand_string | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
md="bench/results/$stamp-$host-tls-backends.md"
[ -n "$XISTO_AWSLC" ] || { echo "set XISTO_AWSLC"; exit 1; }
XISTO_TLS= ./build/xisto build -o $O/tls-bearssl tests/tls/server.ts > /dev/null || exit 1
XISTO_TLS=awslc ./build/xisto build -o $O/tls-awslc tests/tls/server.ts > /dev/null || exit 1
cpu() { ps -o time= -p $1 | awk -F'[:.]' '{ if (NF==3) print ($1*60+$2)+("0."$3); else print ($1*3600+$2*60+$3)+("0."$4) }'; }
rss() { echo $(( $(ps -o rss= -p $1) / 1024 )); }
oha_json() { oha "$@" --insecure --no-tui --output-format json "$URL" > $O/cmp.json 2>/dev/null; python3 -c "
import json; d=json.load(open('$O/cmp.json')); print('%.0f %.2f' % (d['summary']['requestsPerSec'], d['latencyPercentiles']['p99']*1000))"; }
idle_conns() { # opens 1000 TLS connections, one request each, keeps them open; prints KB of server RSS per connection
    python3 - $1 <<'PY'
import socket, ssl, subprocess, sys, time
pid = sys.argv[1]
def rss(): return int(subprocess.run(["ps", "-o", "rss=", "-p", pid], capture_output=True, text=True).stdout)
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
before = rss(); conns = []
for _ in range(1000):
    s = ctx.wrap_socket(socket.create_connection(("127.0.0.1", 8443)), server_hostname="localhost")
    s.sendall(b"GET /hello?name=isa HTTP/1.1\r\nHost: x\r\n\r\n"); s.recv(4096); conns.append(s)
time.sleep(0.5)
print("%.1f" % ((rss() - before) / 1000.0), conns[0].version())
for s in conns: s.close()
PY
}
{
    echo "# TLS backends $stamp"; echo
    echo "Machine: $(sysctl -n machdep.cpu.brand_string). node $(node --version), bun $(bun --version). EC P-256 certificate. oha negotiates the highest protocol each server offers."; echo
    echo "| server | protocol | keep-alive req/s | p99 ms | RSS MB | handshakes/s | p99 ms | server CPU per handshake | KB per idle connection | executable |"
    echo "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|"
} > $md
for t in bearssl awslc node bun; do
    case $t in
        bearssl) $O/tls-bearssl > /dev/null 2>&1 & ;;
        awslc) $O/tls-awslc > /dev/null 2>&1 & ;;
        node) node tests/tls/server.node.ts > /dev/null 2>&1 & ;;
        bun) bun tests/tls/server.bun.ts > /dev/null 2>&1 & ;;
    esac
    pid=$!
    until curl -sk -o /dev/null "$URL"; do sleep 0.05; done
    oha -n 300 -c 8 --disable-keepalive --insecure --no-tui "$URL" > /dev/null 2>&1
    set -- $(idle_conns $pid); kbconn=$1; proto=$2
    set -- $(oha_json -z $DUR -c 64); ka=$1; kap=$2; karss=$(rss $pid)
    set -- $(oha_json -z $DUR -c 32 --disable-keepalive); hs=$1; hsp=$2
    c0=$(cpu $pid); oha -n 5000 -c 8 --disable-keepalive --insecure --no-tui "$URL" > /dev/null 2>&1; c1=$(cpu $pid)
    us=$(echo "$c0 $c1" | awk '{ printf "%.0f", ($2-$1)*1e6/5000 }')
    case $t in bearssl|awslc) size="$(( $(stat -f%z $O/tls-$t) / 1024 )) KB" ;; *) size="runtime" ;; esac
    kill $pid; wait $pid 2>/dev/null; sleep 0.5
    echo "| $t | $proto | $ka | $kap | $karss | $hs | $hsp | $us us | $kbconn | $size |" >> $md
    printf "%-8s %-8s keep-alive %7s req/s p99 %5s ms rss %4s MB | handshakes %5s/s p99 %6s ms | %4s us CPU/handshake | %5s KB/idle conn | %s\n" $t $proto $ka $kap $karss $hs $hsp $us $kbconn "$size"
done
echo "-> $md"
