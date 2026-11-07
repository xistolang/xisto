#!/bin/sh
# HTTP bench: the three servers under the same oha load, then the three clients against the Xisto server.
# Usage: bench/http/run.sh [DURATION]   (default 10s per route)
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.." || exit 1
DUR=${1:-10s}
CONC=64
XISTO=./build/xisto
OUT=bench/out
mkdir -p "$OUT" bench/results
stamp=$(date +%Y-%m-%d)
host=$(sysctl -n machdep.cpu.brand_string 2>/dev/null | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
csv="bench/results/$stamp-$host-http.csv"
md="bench/results/$stamp-$host-http.md"
NODE=$(command -v node); BUN=$(command -v bun); OHA=$(command -v oha)
[ -n "$OHA" ] || { echo "oha not found (brew install oha)"; exit 1; }

ship_size() {
    exe=$(realpath "$1"); total=$(stat -Lf%z "$exe")
    for lib in $(otool -L "$exe" 2>/dev/null | awk '/@rpath\/lib(node|bun)/{print $1}' | sed 's|@rpath/||'); do
        f="$(dirname "$exe")/../lib/$lib"; [ -f "$f" ] && total=$((total + $(stat -Lf%z "$f")))
    done
    echo "$total"
}
mb() { echo "$1" | awk '{printf "%.2f", $1/1048576}'; }

$XISTO build -o "$OUT/server-xisto" bench/http/server.xisto.ts || exit 1
$XISTO build -o "$OUT/client-xisto" bench/http/client.ts || exit 1

echo "kind,target,route,duration,req_per_s,p50_ms,p99_ms,errors,rss_mb,startup_ms,executable_bytes" > "$csv"
{
    echo "# HTTP bench $stamp"
    echo
    echo "Machine: $(sysctl -n machdep.cpu.brand_string), macOS $(sw_vers -productVersion). node $($NODE --version), bun $($BUN --version), oha $($OHA --version | awk '{print $2}')."
    echo "Load: oha, $CONC connections, $DUR per route, keep-alive. Servers on 127.0.0.1:8080, one process each. RSS sampled right after the load. Errors exclude the connections oha aborts at the deadline. Startup = ms from spawn to the first 200 on /ping."
    echo
    echo "## Servers"
    echo
    echo "| server | route | req/s | p50 (ms) | p99 (ms) | errors | RSS (MB) | startup (ms) | executable (MB) |"
    echo "|---|---|---:|---:|---:|---:|---:|---:|---:|"
} > "$md"

wait_ready() { # prints ms from now until the first successful /ping (polls every millisecond)
    python3 - <<'PYEOF'
import socket, time
t0 = time.time()
while time.time() - t0 < 30:
    try:
        s = socket.create_connection(("127.0.0.1", 8080), timeout=0.2)
        s.sendall(b"GET /ping HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        data = s.recv(4096); s.close()
        if b"200" in data:
            print(int((time.time() - t0) * 1000)); break
    except OSError:
        time.sleep(0.001)
PYEOF
}

run_server() { # $1=target $2=exe for size, rest = command
    target=$1; exe=$2; shift 2
    echo "== server $target"
    # warm launch: the first run of a fresh binary pays macOS code-signing verification, and files enter the cache
    "$@" > /dev/null 2>&1 &
    wpid=$!
    wait_ready > /dev/null
    kill $wpid 2>/dev/null; wait $wpid 2>/dev/null
    "$@" > "$OUT/server-$target.log" 2>&1 &
    pid=$!
    startup=$(wait_ready)
    for route in "/ping" "/hello?name=isa"; do
        $OHA -z "$DUR" -c $CONC --no-tui --output-format json "http://127.0.0.1:8080$route" > "$OUT/oha-$target.json"
        rss_kb=$(ps -o rss= -p $pid | tr -d ' ')
        set -- $(python3 -c "
import json; d=json.load(open('$OUT/oha-$target.json')); s=d['summary']; p=d['latencyPercentiles']
errs=sum(v for k,v in d['statusCodeDistribution'].items() if k!='200')+sum(v for k,v in d['errorDistribution'].items() if 'deadline' not in k and 'aborted' not in k)
print('%.0f %.3f %.3f %d' % (s['requestsPerSec'], p['p50']*1000, p['p99']*1000, errs))")
        size=$(ship_size "$exe")
        echo "server,$target,$route,$DUR,$1,$2,$3,$4,$(mb $((rss_kb*1024))),$startup,$size" >> "$csv"
        echo "| $target | $route | $1 | $2 | $3 | $4 | $(mb $((rss_kb*1024))) | $startup | $(mb $size) |" >> "$md"
        printf "   %-16s %8s req/s  p50 %6s ms  p99 %6s ms  errors %s  rss %s MB\n" "$route" "$1" "$2" "$3" "$4" "$(mb $((rss_kb*1024)))"
    done
    pkill -P $pid 2>/dev/null; kill $pid 2>/dev/null; wait $pid 2>/dev/null
    port_free
}

# the next server must not find the previous one still holding port 8080
port_free() {
    python3 - <<'PYEOF'
import socket, time
t0 = time.time()
while time.time() - t0 < 10:
    try:
        socket.create_connection(("127.0.0.1", 8080), timeout=0.2).close(); time.sleep(0.05)
    except OSError:
        break
PYEOF
}

run_server xisto "$OUT/server-xisto" "$OUT/server-xisto"
run_server node "$NODE" "$NODE" bench/http/server.node.ts
run_server bun "$BUN" "$BUN" bench/http/server.bun.ts

{
    echo
    echo "## Clients"
    echo
    echo "Same file (bench/http/client.ts) on the three runtimes, 20000 iterations of ping + hello, sequential, against the Xisto server. Median of 3."
    echo
    echo "| client | time (s) | RSS (MB) | output |"
    echo "|---|---:|---:|---|"
} >> "$md"
port_free
"$OUT/server-xisto" > /dev/null 2>&1 &
spid=$!
wait_ready > /dev/null
curl -sf http://127.0.0.1:8080/__metrics > /dev/null || { echo "the server on 8080 is not the Xisto server"; exit 1; }
run_client() { # $1=target rest=command
    target=$1; shift
    ts=""; r=0
    for k in 1 2 3; do
        /usr/bin/time -l "$@" > "$OUT/client-$target.out" 2> "$OUT/client-$target.time"
        ts="$ts $(awk '/real/{print $1}' "$OUT/client-$target.time")"
        rr=$(awk '/maximum resident set size/{print $1}' "$OUT/client-$target.time"); [ "$rr" -gt "$r" ] && r=$rr
    done
    t=$(echo $ts | tr ' ' '\n' | sort -n | sed -n 2p)
    o=$(cat "$OUT/client-$target.out")
    echo "client,$target,-,-,-,-,-,-,$(mb $r),$t,-" >> "$csv"
    echo "| $target | $t | $(mb $r) | $o |" >> "$md"
    printf "   client %-6s %6ss  rss %s MB  %s\n" "$target" "$t" "$(mb $r)" "$o"
}
echo "== clients (against the xisto server)"
run_client xisto "$OUT/client-xisto"
run_client node "$NODE" bench/http/client.ts
run_client bun "$BUN" bench/http/client.ts
kill $spid 2>/dev/null; wait $spid 2>/dev/null
echo; echo "results: $csv"; echo "         $md"
