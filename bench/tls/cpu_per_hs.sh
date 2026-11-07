#!/bin/sh
# Server CPU per full TLS handshake: 5000 one-request connections (oha --disable-keepalive), CPU time of the
# server process before/after. Unlike handshakes/s, this does not depend on how fast the load generator is.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.." || exit 1
N=${1:-5000}
URL="https://127.0.0.1:8443/hello?name=isa"
./build/xisto build -o bench/out/tls-xisto tests/tls/server.ts > /dev/null || exit 1
cpu() { ps -o time= -p $1 | awk -F'[:.]' '{ if (NF==3) print ($1*60+$2)+("0."$3); else print ($1*3600+$2*60+$3)+("0."$4) }'; }
for target in xisto node bun; do
    case $target in
        xisto) bench/out/tls-xisto > /dev/null 2>&1 & ;;
        node) node tests/tls/server.node.ts > /dev/null 2>&1 & ;;
        bun) bun tests/tls/server.bun.ts > /dev/null 2>&1 & ;;
    esac
    pid=$!
    until curl -sk -o /dev/null "$URL"; do sleep 0.05; done
    oha -n 500 -c 8 --disable-keepalive --insecure --no-tui "$URL" > /dev/null 2>&1   # warm-up
    c0=$(cpu $pid)
    oha -n $N -c 8 --disable-keepalive --insecure --no-tui "$URL" > /dev/null 2>&1
    c1=$(cpu $pid)
    echo "$target $c0 $c1" | awk -v n=$N '{ printf "%-6s %6.1f us of server CPU per handshake\n", $1, ($3-$2)*1e6/n }'
    kill $pid; wait $pid 2>/dev/null; sleep 0.5
done
