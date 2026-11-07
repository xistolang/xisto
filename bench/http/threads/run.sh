#!/bin/sh
# Threads bench: one event loop vs N workers, on Xisto, Node (cluster) and Bun (reusePort).
# Records throughput, latency, and the CPU seconds the server(s) burned during the load.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../../.." || exit 1
DUR=${1:-10s}; CONC=${2:-256}
XISTO=./build/xisto; OUT=bench/out; mkdir -p "$OUT" bench/results
stamp=$(date +%Y-%m-%d); host=$(sysctl -n machdep.cpu.brand_string | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
csv="bench/results/$stamp-$host-threads.csv"; md="bench/results/$stamp-$host-threads.md"
NODE=$(command -v node); BUN=$(command -v bun); OHA=$(command -v oha)
$XISTO build -o "$OUT/server-xisto" bench/http/server.xisto.ts || exit 1
$XISTO build -o "$OUT/todo-xisto" bench/http/todo/server.xisto.ts || exit 1
cores=$(sysctl -n hw.ncpu); pcores=$(sysctl -n hw.perflevel0.logicalcpu 2>/dev/null || echo "?")

wait_ready() {
    python3 - "$1" <<'PYEOF'
import socket, sys, time
path = sys.argv[1]
t0 = time.time()
while time.time() - t0 < 30:
    try:
        s = socket.create_connection(("127.0.0.1", 8080), timeout=0.2)
        s.sendall(("GET %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n" % path).encode())
        data = s.recv(4096); s.close()
        if b"200" in data: break
    except OSError:
        time.sleep(0.002)
PYEOF
}
# CPU seconds used so far by a process and all its descendants
cpu_tree() {
    python3 - "$1" <<'PYEOF'
import subprocess, sys
root = int(sys.argv[1])
rows = subprocess.run(["ps", "-axo", "pid=,ppid=,time="], capture_output=True, text=True).stdout.split("\n")
kids = {}; cpu = {}
def secs(t):
    parts = t.strip().split(":")
    s = 0.0
    for p in parts: s = s * 60 + float(p)
    return s
for r in rows:
    f = r.split()
    if len(f) < 3: continue
    pid, ppid = int(f[0]), int(f[1])
    kids.setdefault(ppid, []).append(pid); cpu[pid] = secs(f[2])
total = 0.0; stack = [root]
while stack:
    p = stack.pop(); total += cpu.get(p, 0.0); stack += kids.get(p, [])
print("%.2f" % total)
PYEOF
}
echo "target,workers,route,req_per_s,p50_ms,p99_ms,errors,server_cpu_s,cpu_s_per_100k,rss_mb" > "$csv"
{
    echo "# Threads bench $stamp"; echo
    echo "Machine: $(sysctl -n machdep.cpu.brand_string), $cores logical cores ($pcores performance). macOS $(sw_vers -productVersion). node $($NODE --version), bun $($BUN --version), oha $($OHA --version | awk '{print $2}')."
    echo "Load: oha, $CONC connections, $DUR per route, keep-alive, on the same machine (the load generator competes for cores). \"server CPU\" = CPU seconds burned by the server process tree during the load; \"CPU/100k\" = that divided by requests, times 100 000: the energy proxy."; echo
    echo "| target | workers | route | req/s | p50 (ms) | p99 (ms) | server CPU (s) | CPU s / 100k req | RSS (MB, all processes) |"
    echo "|---|---:|---|---:|---:|---:|---:|---:|---:|"
} > "$md"

run_target() { # $1=label $2=workers $3=ready-path, rest=command (run with env already set by caller)
    label=$1; workers=$2; path=$3; shift 3
    case "$label" in ${ONLY:-*}) ;; *) return ;; esac
    echo "== $label ($workers)"
    "$@" > "$OUT/threads-$label.log" 2>&1 & pid=$!
    wait_ready "$path"
    for route in "/ping" "/hello?name=isa"; do
        cpu0=$(cpu_tree $pid)
        $OHA -z "$DUR" -c $CONC --no-tui --output-format json "http://127.0.0.1:8080$route" > "$OUT/oha-threads.json"
        cpu1=$(cpu_tree $pid)
        rss_kb=$(python3 -c "
import subprocess
rows=subprocess.run(['ps','-axo','pid=,ppid=,rss='],capture_output=True,text=True).stdout.split('\n')
kids={};rss={}
for r in rows:
    f=r.split()
    if len(f)<3: continue
    kids.setdefault(int(f[1]),[]).append(int(f[0])); rss[int(f[0])]=int(f[2])
t=0;st=[$pid]
while st:
    p=st.pop(); t+=rss.get(p,0); st+=kids.get(p,[])
print(t)")
        set -- $(python3 -c "
import json; d=json.load(open('$OUT/oha-threads.json')); s=d['summary']; p=d['latencyPercentiles']
errs=sum(v for k,v in d['statusCodeDistribution'].items() if k!='200')+sum(v for k,v in d['errorDistribution'].items() if 'deadline' not in k and 'aborted' not in k)
total=sum(d['statusCodeDistribution'].values())
cpu=$cpu1-$cpu0
print('%.0f %.3f %.3f %d %.2f %.3f' % (s['requestsPerSec'], p['p50']*1000, p['p99']*1000, errs, cpu, cpu/total*100000 if total else 0))")
        rss_mb=$(echo "$rss_kb" | awk '{printf "%.1f", $1/1024}')
        echo "$label,$workers,$route,$1,$2,$3,$4,$5,$6,$rss_mb" >> "$csv"
        echo "| $label | $workers | $route | $1 | $2 | $3 | $5 | $6 | $rss_mb |" >> "$md"
        printf "   %-16s %8s req/s  p50 %6s  p99 %6s  cpu %5ss  cpu/100k %6s  rss %s MB\n" "$route" "$1" "$2" "$3" "$5" "$6" "$rss_mb"
    done
    pkill -P $pid 2>/dev/null; kill $pid 2>/dev/null; wait $pid 2>/dev/null
    pkill -f 'server.bun.reuseport' 2>/dev/null; true
}

run_target xisto 1 /ping "$OUT/server-xisto"
XISTO_WORKERS=2 XISTO_LOCK=0 run_target xisto 2 /ping "$OUT/server-xisto"
XISTO_WORKERS=4 XISTO_LOCK=0 run_target xisto 4 /ping "$OUT/server-xisto"
XISTO_WORKERS=8 XISTO_LOCK=0 run_target xisto 8 /ping "$OUT/server-xisto"
XISTO_WORKERS=4 XISTO_LOCK=1 run_target xisto-lock 4 /ping "$OUT/server-xisto"
WORKERS=1 run_target node 1 /ping "$NODE" bench/http/threads/server.node.cluster.ts
WORKERS=4 run_target node-cluster 4 /ping "$NODE" bench/http/threads/server.node.cluster.ts
run_target bun 1 /ping "$BUN" bench/http/threads/server.bun.reuseport.ts
bun4() { "$BUN" bench/http/threads/server.bun.reuseport.ts & "$BUN" bench/http/threads/server.bun.reuseport.ts & "$BUN" bench/http/threads/server.bun.reuseport.ts & exec "$BUN" bench/http/threads/server.bun.reuseport.ts; }
run_target bun-reuseport 4 /ping sh -c 'exec "$0" "$@"' sh -c "$BUN bench/http/threads/server.bun.reuseport.ts & $BUN bench/http/threads/server.bun.reuseport.ts & $BUN bench/http/threads/server.bun.reuseport.ts & exec $BUN bench/http/threads/server.bun.reuseport.ts"

{
    echo; echo "## Todo API list route (50 items serialized): shared state"; echo
    echo "| target | workers | req/s | p50 (ms) | p99 (ms) | server CPU (s) | CPU s / 100k |"
    echo "|---|---:|---:|---:|---:|---:|---:|"
} >> "$md"
run_todo() { # $1=label $2=workers rest=command
    label=$1; workers=$2; shift 2
    case "$label" in ${ONLY:-*}) ;; *) return ;; esac
    echo "== todo $label ($workers)"
    "$@" > "$OUT/threads-todo-$label.log" 2>&1 & pid=$!
    wait_ready /todos
    i=1; while [ $i -le 50 ]; do curl -s -o /dev/null -X POST -d "{\"title\":\"todo $i\"}" http://127.0.0.1:8080/todos; i=$((i+1)); done
    cpu0=$(cpu_tree $pid)
    $OHA -z "$DUR" -c $CONC --no-tui --output-format json "http://127.0.0.1:8080/todos" > "$OUT/oha-threads.json"
    cpu1=$(cpu_tree $pid)
    set -- $(python3 -c "
import json; d=json.load(open('$OUT/oha-threads.json')); s=d['summary']; p=d['latencyPercentiles']
total=sum(d['statusCodeDistribution'].values()); cpu=$cpu1-$cpu0
print('%.0f %.3f %.3f %.2f %.3f' % (s['requestsPerSec'], p['p50']*1000, p['p99']*1000, cpu, cpu/total*100000 if total else 0))")
    echo "todo-$label,$workers,/todos,$1,$2,$3,0,$4,$5,-" >> "$csv"
    echo "| $label | $workers | $1 | $2 | $3 | $4 | $5 |" >> "$md"
    printf "   list %8s req/s  p50 %6s  p99 %6s  cpu %5ss  cpu/100k %s\n" "$1" "$2" "$3" "$4" "$5"
    kill $pid 2>/dev/null; wait $pid 2>/dev/null
}
run_todo xisto 1 "$OUT/todo-xisto"
XISTO_WORKERS=4 XISTO_LOCK=1 run_todo xisto-lock 4 "$OUT/todo-xisto"
XISTO_WORKERS=4 XISTO_LOCK=0 run_todo xisto-nolock-reads 4 "$OUT/todo-xisto"
echo; echo "results: $csv"; echo "         $md"
