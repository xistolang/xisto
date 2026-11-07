#!/bin/sh
# Todo API bench: the three servers seeded with 50 todos, then oha on list, read and update.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../../.." || exit 1
DUR=${1:-10s}; CONC=64
XISTO=./build/xisto; OUT=bench/out; mkdir -p "$OUT" bench/results
stamp=$(date +%Y-%m-%d); host=$(sysctl -n machdep.cpu.brand_string | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
csv="bench/results/$stamp-$host-todo.csv"; md="bench/results/$stamp-$host-todo.md"
NODE=$(command -v node); BUN=$(command -v bun); OHA=$(command -v oha)
ship_size() {
    exe=$(realpath "$1"); total=$(stat -Lf%z "$exe")
    for lib in $(otool -L "$exe" 2>/dev/null | awk '/@rpath\/lib(node|bun)/{print $1}' | sed 's|@rpath/||'); do
        f="$(dirname "$exe")/../lib/$lib"; [ -f "$f" ] && total=$((total + $(stat -Lf%z "$f")))
    done; echo "$total"
}
mb() { echo "$1" | awk '{printf "%.2f", $1/1048576}'; }
wait_ready() {
    python3 - <<'PYEOF'
import socket, time
t0 = time.time()
while time.time() - t0 < 30:
    try:
        s = socket.create_connection(("127.0.0.1", 8080), timeout=0.2)
        s.sendall(b"GET /todos HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        data = s.recv(4096); s.close()
        if b"200" in data:
            print(int((time.time() - t0) * 1000)); break
    except OSError:
        time.sleep(0.001)
PYEOF
}
$XISTO build -o "$OUT/todo-xisto" bench/http/todo/server.xisto.ts || exit 1
echo "server,route,req_per_s,p50_ms,p99_ms,errors,rss_mb,startup_ms,executable_bytes" > "$csv"
{
    echo "# Todo API bench $stamp"; echo
    echo "Machine: $(sysctl -n machdep.cpu.brand_string), macOS $(sw_vers -productVersion). node $($NODE --version), bun $($BUN --version), oha $($OHA --version | awk '{print $2}')."
    echo "Each server is seeded with 50 todos, then oha runs $CONC connections for $DUR per route. Routes: list (GET /todos, 50 items serialized), read (GET /todos/25), update (PUT /todos/25 with a JSON body). Errors exclude connections aborted at the deadline."; echo
    echo "| server | route | req/s | p50 (ms) | p99 (ms) | errors | RSS (MB) | startup (ms) | executable (MB) |"
    echo "|---|---|---:|---:|---:|---:|---:|---:|---:|"
} > "$md"
run_server() {
    target=$1; exe=$2; shift 2
    echo "== $target"
    "$@" > /dev/null 2>&1 & wpid=$!; wait_ready > /dev/null; kill $wpid 2>/dev/null; wait $wpid 2>/dev/null
    "$@" > "$OUT/todo-$target.log" 2>&1 & pid=$!
    startup=$(wait_ready)
    i=1; while [ $i -le 50 ]; do curl -s -o /dev/null -X POST -d "{\"title\":\"todo $i\"}" http://127.0.0.1:8080/todos; i=$((i+1)); done
    for spec in "list|GET|/todos|" "read|GET|/todos/25|" "update|PUT|/todos/25|{\"title\":\"todo 25\",\"done\":true}"; do
        name=${spec%%|*}; rest=${spec#*|}; method=${rest%%|*}; rest=${rest#*|}; route=${rest%%|*}; body=${rest#*|}
        if [ -n "$body" ]; then $OHA -z "$DUR" -c $CONC --no-tui --output-format json -m "$method" -d "$body" -T application/json "http://127.0.0.1:8080$route" > "$OUT/oha-todo.json"
        else $OHA -z "$DUR" -c $CONC --no-tui --output-format json "http://127.0.0.1:8080$route" > "$OUT/oha-todo.json"; fi
        rss_kb=$(ps -o rss= -p $pid | tr -d ' ')
        set -- $(python3 -c "
import json; d=json.load(open('$OUT/oha-todo.json')); s=d['summary']; p=d['latencyPercentiles']
errs=sum(v for k,v in d['statusCodeDistribution'].items() if k not in ('200','201'))+sum(v for k,v in d['errorDistribution'].items() if 'deadline' not in k and 'aborted' not in k)
print('%.0f %.3f %.3f %d' % (s['requestsPerSec'], p['p50']*1000, p['p99']*1000, errs))")
        size=$(ship_size "$exe")
        echo "$target,$name,$1,$2,$3,$4,$(mb $((rss_kb*1024))),$startup,$size" >> "$csv"
        echo "| $target | $name | $1 | $2 | $3 | $4 | $(mb $((rss_kb*1024))) | $startup | $(mb $size) |" >> "$md"
        printf "   %-8s %8s req/s  p50 %6s ms  p99 %6s ms  errors %s  rss %s MB\n" "$name" "$1" "$2" "$3" "$4" "$(mb $((rss_kb*1024)))"
    done
    curl -s http://127.0.0.1:8080/__metrics > "$OUT/metrics-$target.json" 2>/dev/null
    kill $pid 2>/dev/null; wait $pid 2>/dev/null
}
run_server xisto "$OUT/todo-xisto" "$OUT/todo-xisto"
run_server node "$NODE" "$NODE" bench/http/todo/server.node.ts
run_server bun "$BUN" "$BUN" bench/http/todo/server.bun.ts
echo; echo "results: $csv"; echo "         $md"
