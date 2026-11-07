#!/bin/sh
# Energy bench: joules per task measured with powermetrics (needs sudo; run it yourself):
#   sudo sh bench/energy/run.sh
# For each target it samples CPU package power every 100 ms while the workload runs, integrates to joules,
# subtracts the idle baseline measured the same way, and reports joules per task and per 100k requests.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.." || exit 1
[ "$(id -u)" = 0 ] || { echo "powermetrics needs root: sudo sh bench/energy/run.sh"; exit 1; }
USER_HOME=$(eval echo "~${SUDO_USER:-$USER}")
export PATH="/opt/homebrew/bin:$USER_HOME/.bun/bin:$PATH"
OUT=bench/out; mkdir -p "$OUT" bench/results
stamp=$(date +%Y-%m-%d); host=$(sysctl -n machdep.cpu.brand_string | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
csv="bench/results/$stamp-$host-energy.csv"; md="bench/results/$stamp-$host-energy.md"
NODE=$(command -v node); BUN=$(command -v bun); OHA=$(command -v oha)
X=./build/xisto
run_as_user() { sudo -u "${SUDO_USER:-$USER}" "$@"; }
run_as_user $X build -o "$OUT/sieve-x" bench/programs/sieve.ts
run_as_user $X build -o "$OUT/srv-x" bench/http/todo/server.xisto.ts
run_as_user $X build -o "$OUT/count-x" bench/streams/count-stream.xisto.ts

# measure_cmd LABEL -- command ... : runs powermetrics in parallel, prints joules above idle and seconds
measure() {
    label=$1; shift
    powermetrics --samplers cpu_power -i 100 -o "$OUT/pm-$label.txt" > /dev/null 2>&1 &
    pm=$!
    sleep 0.5
    t0=$(python3 -c 'import time; print(time.time())')
    "$@" > /dev/null 2>&1
    t1=$(python3 -c 'import time; print(time.time())')
    sleep 0.3
    kill $pm; wait $pm 2>/dev/null
    python3 - "$OUT/pm-$label.txt" "$t0" "$t1" "$IDLE_W" <<'PYEOF'
import re, sys
path, t0, t1, idle = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
watts = [int(m) / 1000 for m in re.findall(r"Combined Power \(CPU \+ GPU \+ ANE\): (\d+) mW", open(path).read())]
if not watts: watts = [int(m) / 1000 for m in re.findall(r"CPU Power: (\d+) mW", open(path).read())]
secs = t1 - t0
avg = sum(watts) / len(watts) if watts else 0
print("%.3f %.2f %.2f" % (max(avg - idle, 0) * secs, secs, avg))
PYEOF
}
echo "measuring idle..."
IDLE_W=0
set -- $(measure idle sleep 5)
IDLE_W=$(python3 -c "print($1/$2 if $2 else 0)" 2>/dev/null || echo 0)
IDLE_W=$(echo "$3")
echo "idle: $IDLE_W W"

echo "workload,target,joules_above_idle,seconds,avg_watts,unit,joules_per_unit" > "$csv"
{
    echo "# Energy bench $stamp"; echo
    echo "powermetrics cpu_power, 100 ms samples, idle baseline $IDLE_W W subtracted. Machine: $(sysctl -n machdep.cpu.brand_string)."; echo
    echo "| workload | target | joules above idle | seconds | avg W | joules per unit |"
    echo "|---|---|---:|---:|---:|---:|"
} > "$md"
record() { # workload target unit units -- measure output
    w=$1; t=$2; unit=$3; units=$4; j=$5; s=$6; a=$7
    per=$(python3 -c "print('%.4f' % ($j / $units))")
    echo "$w,$t,$j,$s,$a,$unit,$per" >> "$csv"
    echo "| $w | $t | $j | $s | $a | $per J / $unit |" >> "$md"
    printf "  %-10s %-6s %8s J  %6s s  %6s W  %s J/%s\n" "$w" "$t" "$j" "$s" "$a" "$per" "$unit"
}
LOG=$OUT/big.log
for t in xisto node bun; do
    case $t in
        xisto) set -- $(measure sieve-$t sh -c "for i in 1 2 3 4 5; do $OUT/sieve-x; done") ;;
        node) set -- $(measure sieve-$t sh -c "for i in 1 2 3 4 5; do run_as_user=1 $NODE bench/programs/sieve.ts; done") ;;
        bun) set -- $(measure sieve-$t sh -c "for i in 1 2 3 4 5; do $BUN bench/programs/sieve.ts; done") ;;
    esac
    record sieve $t run 5 "$@"
done
if [ -f "$LOG" ]; then
    set -- $(measure stream-xisto $OUT/count-x "$LOG"); record stream-1GB xisto run 1 "$@"
    set -- $(measure stream-node $NODE bench/streams/count-stream.node.ts "$LOG"); record stream-1GB node run 1 "$@"
    set -- $(measure stream-bun $BUN bench/streams/count-stream.bun.ts "$LOG"); record stream-1GB bun run 1 "$@"
fi
# HTTP: 10 s of load on GET /todos (50 items) per server; joules per 100k requests
for t in xisto node bun; do
    case $t in
        xisto) run_as_user $OUT/srv-x > /dev/null 2>&1 & ;;
        node) run_as_user $NODE bench/http/todo/server.node.ts > /dev/null 2>&1 & ;;
        bun) run_as_user $BUN bench/http/todo/server.bun.ts > /dev/null 2>&1 & ;;
    esac
    sp=$!
    sleep 1.5
    i=1; while [ $i -le 50 ]; do curl -s -o /dev/null -X POST -d "{\"title\":\"t$i\"}" http://127.0.0.1:8080/todos; i=$((i+1)); done
    set -- $(measure http-$t $OHA -z 10s -c 64 --no-tui --output-format json -o "$OUT/oha-energy-$t.json" http://127.0.0.1:8080/todos)
    reqs=$(python3 -c "import json; print(sum(json.load(open('$OUT/oha-energy-$t.json'))['statusCodeDistribution'].values()))")
    record http-list $t "100k req" "$(python3 -c "print($reqs/100000)")" "$@"
    pkill -P $sp 2>/dev/null; kill $sp 2>/dev/null; wait $sp 2>/dev/null
done
echo; echo "results: $csv"; echo "         $md"
echo "note: the load generator (oha) runs on the same machine and its power is included in every HTTP row."
