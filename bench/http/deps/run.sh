#!/bin/sh
# Slow-dependency bench: handlers that wait on a fake database answering after N ms.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../../.." || exit 1
ulimit -n 16384 2>/dev/null
XISTO=./build/xisto; OUT=bench/out; mkdir -p "$OUT" bench/results
stamp=$(date +%Y-%m-%d); host=$(sysctl -n machdep.cpu.brand_string | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
csv="bench/results/$stamp-$host-deps.csv"; md="bench/results/$stamp-$host-deps.md"
NODE=$(command -v node); BUN=$(command -v bun); OHA=$(command -v oha)
CONC=${CONC:-64}
$XISTO build -o "$OUT/app-xisto" bench/http/deps/app.xisto.ts || exit 1
[ -d node_modules/undici ] || npm install --silent --no-save undici > /dev/null 2>&1

wait_port() { # $1=port $2=path
    python3 - "$1" "$2" <<'PYEOF'
import socket, sys, time
port, path = int(sys.argv[1]), sys.argv[2]
t0 = time.time()
while time.time() - t0 < 30:
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=1)
        s.sendall(("GET %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n" % path).encode())
        if b"200" in s.recv(4096): s.close(); break
        s.close()
    except OSError:
        time.sleep(0.005)
PYEOF
}
cpu_of() { ps -o time= -p "$1" | awk -F'[:.]' '{ if (NF==3) print $1*60+$2+$3/100; else print $1*3600+$2*60+$3+$4/100 }'; }

"$BUN" bench/http/deps/db.bun.ts > "$OUT/db.log" 2>&1 & dbpid=$!
wait_port 8081 "/q?ms=0"

echo "target,route,latency_ms,req_per_s,p50_ms,p99_ms,timeouts,server_cpu_s,cpu_s_per_100k,rss_mb" > "$csv"
{
    echo "# Slow dependency bench $stamp"; echo
    echo "Machine: $(sysctl -n machdep.cpu.brand_string). node $($NODE --version), bun $($BUN --version), oha $($OHA --version | awk '{print $2}'). Fake database on Bun (8081) answering after N ms. Load: oha, $CONC connections, keep-alive. Routes: one = 1 call, three = 3 sequential calls, mixed = 1 call + 50 items serialized."; echo
    echo "| target | route | dependency | req/s | p50 (ms) | p99 (ms) | timeouts | CPU s / 100k | RSS (MB) |"
    echo "|---|---|---:|---:|---:|---:|---:|---:|---:|"
} > "$md"

run_target() { # $1=label rest=command
    label=$1; shift
    case "$label" in ${ONLY:-*}) ;; *) return ;; esac
    echo "== $label"
    "$@" > "$OUT/deps-$label.log" 2>&1 & pid=$!
    wait_port 8080 "/one?ms=0"
    for spec in "one|1" "one|5" "one|20" "one|100" "one|1000" "one|5000" "three|5" "three|100" "three|1000" "mixed|5" "mixed|100"; do
        route=${spec%%|*}; ms=${spec#*|}
        dur=10s; [ "$ms" -ge 1000 ] && dur=20s
        cpu0=$(cpu_of $pid)
        $OHA -z "$dur" -c $CONC -t 30s --no-tui --output-format json "http://127.0.0.1:8080/$route?ms=$ms" > "$OUT/oha-deps.json" 2>/dev/null
        cpu1=$(cpu_of $pid)
        rss=$(ps -o rss= -p $pid | awk '{printf "%.1f", $1/1024}')
        set -- $(python3 -c "
import json; d=json.load(open('$OUT/oha-deps.json')); s=d['summary']; p=d['latencyPercentiles']
ed=d['errorDistribution']; sc=d['statusCodeDistribution']
to=sum(v for k,v in ed.items() if 'timeout' in k.lower())
total=sum(sc.values()); cpu=$cpu1-$cpu0
print('%.1f %.1f %.1f %d %.2f %.3f' % (s['requestsPerSec'], p['p50']*1000, p['p99']*1000, to, cpu, cpu/total*100000 if total else 0))")
        echo "$label,$route,$ms,$1,$2,$3,$4,$5,$6,$rss" >> "$csv"
        echo "| $label | $route | $ms ms | $1 | $2 | $3 | $4 | $6 | $rss |" >> "$md"
        printf "   %-6s %5s ms  %9s req/s  p50 %9s  p99 %9s  timeouts %-5s cpu/100k %s\n" "$route" "$ms" "$1" "$2" "$3" "$4" "$6"
    done
    kill $pid 2>/dev/null; wait $pid 2>/dev/null
}
run_target xisto-1 "$OUT/app-xisto"
XISTO_WORKERS=4 XISTO_LOCK=0 run_target xisto-4 "$OUT/app-xisto"
XISTO_WORKERS=16 XISTO_LOCK=0 run_target xisto-16 "$OUT/app-xisto"
run_target node "$NODE" bench/http/deps/app.node.ts
run_target bun "$BUN" bench/http/deps/app.bun.ts
kill $dbpid 2>/dev/null; wait $dbpid 2>/dev/null
echo; echo "results: $csv"; echo "         $md"
