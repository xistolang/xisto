#!/bin/sh
# Resilience bench: ramp-up, overload, error storm, idle connections, soak, recovery.
# Targets: xisto (1 worker), xisto-4 (4 workers, lock), node, bun, rust (axum, tokio current_thread), go (net/http,
# GOMAXPROCS=1). All on the todo API (shared state, JSON).
# RAMP (default 5s) and PHASE (default 10s) shorten the steps for a smoke run: RAMP=1s PHASE=1s SOAK=1s.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../../.." || exit 1
ulimit -n 16384 2>/dev/null
XISTO=./build/xisto; OUT=bench/out; mkdir -p "$OUT" bench/results
stamp=$(date +%Y-%m-%d); host=$(sysctl -n machdep.cpu.brand_string | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
csv="bench/results/$stamp-$host-resilience.csv"; md="bench/results/$stamp-$host-resilience.md"
NODE=$(command -v node); BUN=$(command -v bun); OHA=$(command -v oha)
SOAK=${SOAK:-60s}; RAMP=${RAMP:-5s}; PHASE=${PHASE:-10s}
$XISTO build -o "$OUT/todo-xisto" bench/http/todo/server.xisto.ts || exit 1
sh bench/http/build-native.sh || exit 1

wait_ready() {
    python3 - <<'PYEOF'
import socket, time
t0 = time.time()
while time.time() - t0 < 30:
    try:
        s = socket.create_connection(("127.0.0.1", 8080), timeout=0.2)
        s.sendall(b"GET /todos HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        data = s.recv(4096); s.close()
        if b"200" in data: break
    except OSError:
        time.sleep(0.002)
PYEOF
}
rss_tree() {
    python3 -c "
import subprocess
rows=subprocess.run(['ps','-axo','pid=,ppid=,rss='],capture_output=True,text=True).stdout.split('\n')
kids={};rss={}
for r in rows:
    f=r.split()
    if len(f)<3: continue
    kids.setdefault(int(f[1]),[]).append(int(f[0])); rss[int(f[0])]=int(f[2])
t=0;st=[$1]
while st:
    p=st.pop(); t+=rss.get(p,0); st+=kids.get(p,[])
print('%.1f' % (t/1024))"
}
# footprint_tree PID: the macOS physical footprint (what Activity Monitor shows) of PID and its children, in MB; empty
# elsewhere. RSS on macOS still counts the pages the allocator gave back as reusable until the kernel needs them.
footprint_tree() {
    [ "$(uname)" = Darwin ] || return 0
    pids="$1 $(pgrep -P "$1" | tr '\n' ' ')"
    for p in $pids; do footprint "$p" 2>/dev/null | awk '/phys_footprint:/{v=$2; u=$3; if (u=="KB") v/=1024; if (u=="GB") v*=1024; print v; exit}'; done |
        awk '{t+=$1} END {if (NR) printf "%.1f", t}'
}
alive() { curl -s -m 2 -o /dev/null -w '%{http_code}' http://127.0.0.1:8080/todos/1; }
oha_json() { # $1=duration $2=conc rest=oha args ; prints "rps p50 p99 errors timeouts"
    d=$1; c=$2; shift 2
    $OHA -z "$d" -c "$c" --no-tui --output-format json "$@" > "$OUT/oha-res.json" 2>/dev/null
    python3 -c "
import json; d=json.load(open('$OUT/oha-res.json')); s=d['summary']; p=d['latencyPercentiles']
ed=d['errorDistribution']; sc=d['statusCodeDistribution']
timeouts=sum(v for k,v in ed.items() if 'timeout' in k.lower())
errs=sum(v for k,v in ed.items() if 'deadline' not in k and 'aborted' not in k)
bad=sum(v for k,v in sc.items() if k not in ('200','201','400','404'))
print('%.0f %.3f %.3f %d %d %d' % (s['requestsPerSec'], p['p50']*1000, p['p99']*1000, errs, timeouts, bad))"
}

echo "target,phase,param,req_per_s,p50_ms,p99_ms,conn_errors,timeouts,bad_status,rss_mb,alive,footprint_mb" > "$csv"
{
    echo "# Resilience bench $stamp"; echo
    echo "Machine: $(sysctl -n machdep.cpu.brand_string), macOS $(sw_vers -productVersion). node $($NODE --version), bun $($BUN --version), oha $($OHA --version | awk '{print $2}'), $(cat $OUT/native-versions.txt). Todo API with 50 items on every server."
    echo "Phases: ramp ($RAMP per concurrency step, GET /todos/25), overload (4096 connections, $PHASE), error storm (POST with invalid JSON, 256 connections, $PHASE: 400 is the right answer), idle (1000 open idle connections while measuring 64 active), soak ($SOAK at 64 connections on GET /todos, RSS sampled), recovery (64 connections after everything). alive = status of a request after each phase."; echo
    echo "| target | phase | param | req/s | p50 (ms) | p99 (ms) | conn errors | timeouts | bad status | RSS (MB) | alive | footprint (MB) |"
    echo "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---|---:|"
} > "$md"

record() { # $1=target $2=phase $3=param $4..=oha_json output, then rss
    t=$1; ph=$2; pa=$3; shift 3
    rps=$1; p50=$2; p99=$3; ce=$4; to=$5; bad=$6; rss=$7; al=$8; fp=$9
    echo "$t,$ph,$pa,$rps,$p50,$p99,$ce,$to,$bad,$rss,$al,$fp" >> "$csv"
    echo "| $t | $ph | $pa | $rps | $p50 | $p99 | $ce | $to | $bad | $rss | $al | $fp |" >> "$md"
    printf "   %-10s %-8s %8s req/s  p50 %7s  p99 %8s  err %s/%s/%s  rss %s MB  alive %s\n" "$ph" "$pa" "$rps" "$p50" "$p99" "$ce" "$to" "$bad" "$rss" "$al"
}

run_target() { # $1=label rest=command
    label=$1; shift
    case "$label" in ${ONLY:-*}) ;; *) return ;; esac
    echo "== $label"
    "$@" > "$OUT/res-$label.log" 2>&1 & pid=$!
    wait_ready
    i=1; while [ $i -le 50 ]; do curl -s -o /dev/null -X POST -d "{\"title\":\"todo $i\"}" http://127.0.0.1:8080/todos; i=$((i+1)); done
    for c in 1 8 64 256 1024 4096; do
        set -- $(oha_json $RAMP $c http://127.0.0.1:8080/todos/25)
        record "$label" ramp "c=$c" "$@" "$(rss_tree $pid)" "$(alive)" "$(footprint_tree $pid)"
    done
    set -- $(oha_json $PHASE 4096 http://127.0.0.1:8080/todos/25)
    record "$label" overload "c=4096" "$@" "$(rss_tree $pid)" "$(alive)" "$(footprint_tree $pid)"
    set -- $(oha_json $PHASE 256 -m POST -d '{"title": ' -T application/json http://127.0.0.1:8080/todos)
    record "$label" error-storm "bad JSON" "$@" "$(rss_tree $pid)" "$(alive)" "$(footprint_tree $pid)"
    # idle connections: open 1000 sockets that never send a full request, then measure with 64 active
    python3 - "$(( ${PHASE%s} + 4 ))" <<'PYEOF' &
import socket, sys, time
socks = []
for i in range(1000):
    try:
        s = socket.create_connection(("127.0.0.1", 8080)); s.sendall(b"GET /todos HTTP/1.1\r\nHost: x\r\n"); socks.append(s)
    except OSError:
        break
time.sleep(int(sys.argv[1]))
for s in socks: s.close()
PYEOF
    idle_pid=$!
    sleep 2
    set -- $(oha_json $PHASE 64 http://127.0.0.1:8080/todos/25)
    record "$label" idle "1000 idle" "$@" "$(rss_tree $pid)" "$(alive)" "$(footprint_tree $pid)"
    wait $idle_pid 2>/dev/null
    rss_before=$(rss_tree $pid)
    set -- $(oha_json "$SOAK" 64 http://127.0.0.1:8080/todos)
    record "$label" soak "$SOAK list" "$@" "$(rss_tree $pid) (before: $rss_before)" "$(alive)" "$(footprint_tree $pid)"
    set -- $(oha_json $PHASE 64 http://127.0.0.1:8080/todos/25)
    record "$label" recovery "c=64" "$@" "$(rss_tree $pid)" "$(alive)" "$(footprint_tree $pid)"
    pkill -P $pid 2>/dev/null; kill $pid 2>/dev/null; wait $pid 2>/dev/null
}

run_target xisto "$OUT/todo-xisto"
XISTO_WORKERS=4 XISTO_LOCK=1 run_target xisto-4 "$OUT/todo-xisto"
run_target node "$NODE" bench/http/todo/server.node.ts
run_target bun "$BUN" bench/http/todo/server.bun.ts
[ -x "$OUT/rust-todo" ] && WORKERS=1 run_target rust "$OUT/rust-todo"
[ -x "$OUT/go-todo" ] && GOMAXPROCS=1 run_target go "$OUT/go-todo"
echo; echo "results: $csv"; echo "         $md"
