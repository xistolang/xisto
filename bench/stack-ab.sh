#!/bin/sh
# Cost of stack-trace support: the same compiler, with and without it (XISTO_NO_TRACE=1), runs interleaved.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/.." || exit 1
X=./build/xisto; O=bench/out/stack-ab; mkdir -p $O; RUNS=${RUNS:-9}
now() { perl -MTime::HiRes=time -e 'printf "%.4f\n", time'; }
med() { sort -n | awk '{a[NR]=$1} END{print a[int((NR+1)/2)]}'; }
echo "== CPU (median of $RUNS, seconds)"
for p in ${PROGS:-fib sieve nbody mandelbrot objects}; do
  for be in c qbe; do
    rm -rf bench/programs/.xisto
    $X build --backend=$be -o $O/$p-$be-on bench/programs/$p.ts 2>/dev/null || continue
    XISTO_NO_TRACE=1 $X build --backend=$be -o $O/$p-$be-off bench/programs/$p.ts 2>/dev/null || continue
    : > $O/on.t; : > $O/off.t
    for i in $(seq $RUNS); do
      for v in on off; do t0=$(now); $O/$p-$be-$v > /dev/null; t1=$(now); echo "$t1 - $t0" | bc >> $O/$v.t; done
    done
    on=$(med < $O/on.t); off=$(med < $O/off.t)
    printf "%-11s %-4s with %6.3f  without %6.3f  delta %+5.1f%%  size %s vs %s\n" $p $be $on $off $(echo "($on/$off-1)*100" | bc -l) $(stat -f%z $O/$p-$be-on) $(stat -f%z $O/$p-$be-off)
  done
done
rm -rf bench/programs/.xisto
echo "== HTTP (oha c64 5s, interleaved x3, req/s)"
$X build -o $O/srv-on bench/http/server.xisto.ts && XISTO_NO_TRACE=1 $X build -o $O/srv-off bench/http/server.xisto.ts
for i in 1 2 3; do for v in on off; do
  $O/srv-$v > /dev/null 2>&1 & S=$!; sleep 0.4
  r=$(oha -z 5s -c 64 --no-tui --output-format json http://127.0.0.1:8080/ping 2>/dev/null | python3 -c 'import json,sys; print(round(json.load(sys.stdin)["summary"]["requestsPerSec"]))')
  echo "ping $v $r"; kill $S; wait $S 2>/dev/null
done; done
