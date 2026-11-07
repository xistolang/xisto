#!/bin/sh
# SQLite bench: the same workload on Xisto (node:sqlite API), Node (node:sqlite) and Bun (bun:sqlite).
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.." || exit 1
OUT=bench/out; mkdir -p $OUT bench/results
stamp=$(date +%Y-%m-%d); host=$(sysctl -n machdep.cpu.brand_string | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
csv="bench/results/$stamp-$host-sqlite.csv"; md="bench/results/$stamp-$host-sqlite.md"
./build/xisto build -o $OUT/sqlite-x bench/sqlite/work.xisto.ts || exit 1
echo "target,time_s,max_rss_mb,executable_mb,result" > $csv
{ echo "# SQLite bench $stamp"; echo; echo "200k inserts in a transaction, 2k point reads, a scan of 198k rows into typed objects. In-memory database. Median of 5."; echo
  echo "| target | time (s) | peak RSS (MB) | executable (MB) | result |"; echo "|---|---:|---:|---:|---|"; } > $md
run() { t=$1; exe=$2; shift 2
  ts=""; rss=0
  for k in 1 2 3 4 5; do /usr/bin/time -l "$@" > $OUT/sq.out 2> $OUT/sq.time; ts="$ts $(awk '/real/{print $1}' $OUT/sq.time)"; r=$(awk '/maximum resident set size/{print $1}' $OUT/sq.time); [ "$r" -gt "$rss" ] && rss=$r; done
  med=$(echo $ts | tr ' ' '\n' | sort -n | sed -n 3p); size=$(stat -Lf%z "$(realpath "$exe")")
  for lib in $(otool -L "$(realpath "$exe")" 2>/dev/null | awk '/@rpath\/lib(node|bun)/{print $1}' | sed 's|@rpath/||'); do f="$(dirname "$(realpath "$exe")")/../lib/$lib"; [ -f "$f" ] && size=$((size + $(stat -Lf%z "$f"))); done
  mb=$(echo $rss | awk '{printf "%.1f", $1/1048576}'); sz=$(echo $size | awk '{printf "%.2f", $1/1048576}')
  echo "$t,$med,$mb,$sz,$(cat $OUT/sq.out)" >> $csv; echo "| $t | $med | $mb | $sz | $(cat $OUT/sq.out) |" >> $md
  printf "  %-6s %6ss %8s MB  exe %s MB  %s\n" $t $med $mb $sz "$(cat $OUT/sq.out)"; }
run xisto $OUT/sqlite-x $OUT/sqlite-x
run node "$(command -v node)" node bench/sqlite/work.node.ts
run bun "$(command -v bun)" bun bench/sqlite/work.bun.ts
echo "results: $md"
