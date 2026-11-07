#!/bin/sh
# Streams bench: count ERROR lines in a 1 GB log, streaming vs reading the whole file, on Xisto, Node and Bun.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.." || exit 1
SIZE_MB=${1:-1024}
XISTO=./build/xisto; OUT=bench/out; mkdir -p "$OUT" bench/results
stamp=$(date +%Y-%m-%d); host=$(sysctl -n machdep.cpu.brand_string | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
csv="bench/results/$stamp-$host-streams.csv"; md="bench/results/$stamp-$host-streams.md"
LOG=$OUT/big.log
if [ ! -f "$LOG" ] || [ "$(stat -f%z "$LOG")" -lt $((SIZE_MB * 1000000)) ]; then
    echo "generating $SIZE_MB MB log..."
    python3 - "$LOG" "$SIZE_MB" <<'PYEOF'
import sys, random
path, mb = sys.argv[1], int(sys.argv[2])
random.seed(7)
levels = ["INFO"] * 9 + ["ERROR"]
with open(path, "w") as f:
    written = 0; i = 0
    while written < mb * 1000000:
        line = "2026-11-05T10:%02d:%02d %s request %d took %d ms path=/todos/%d\n" % (i % 60, (i * 7) % 60, random.choice(levels), i, random.randint(1, 900), i % 50)
        f.write(line); written += len(line); i += 1
PYEOF
fi
$XISTO build -o "$OUT/count-stream-xisto" bench/streams/count-stream.xisto.ts || exit 1
$XISTO build -o "$OUT/count-readfile-xisto" bench/streams/count-readfile.xisto.ts || exit 1
NODE=$(command -v node); BUN=$(command -v bun)
echo "target,mode,time_s,max_rss_mb,result" > "$csv"
{
    echo "# Streams bench $stamp"; echo
    echo "Count the lines containing ERROR in a $(du -h "$LOG" | cut -f1) log ($(wc -l < "$LOG" | tr -d ' ') lines). Median of 3 runs; RSS = peak resident set size."; echo
    echo "| target | mode | time (s) | peak RSS (MB) | result |"
    echo "|---|---|---:|---:|---:|"
} > "$md"
run() { # $1=target $2=mode rest=command
    target=$1; mode=$2; shift 2
    ts=""; rss=0
    for k in 1 2 3; do
        /usr/bin/time -l "$@" "$LOG" > "$OUT/streams.out" 2> "$OUT/streams.time"
        ts="$ts $(awk '/real/{print $1}' "$OUT/streams.time")"
        r=$(awk '/maximum resident set size/{print $1}' "$OUT/streams.time"); [ "$r" -gt "$rss" ] && rss=$r
    done
    t=$(echo "$ts" | tr ' ' '\n' | grep . | sort -n | sed -n 2p)
    res=$(cat "$OUT/streams.out")
    mb=$(echo "$rss" | awk '{printf "%.1f", $1/1048576}')
    echo "$target,$mode,$t,$mb,$res" >> "$csv"
    echo "| $target | $mode | $t | $mb | $res |" >> "$md"
    printf "   %-6s %-9s %6ss  %8s MB  %s\n" "$target" "$mode" "$t" "$mb" "$res"
}
run xisto stream "$OUT/count-stream-xisto"
run xisto readFile "$OUT/count-readfile-xisto"
run node stream "$NODE" bench/streams/count-stream.node.ts
run node readFile "$NODE" bench/streams/count-readfile.node.ts
run bun stream "$BUN" bench/streams/count-stream.bun.ts
echo; echo "results: $csv"; echo "         $md"
