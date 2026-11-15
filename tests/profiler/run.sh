#!/bin/sh
# xisto:profiler: a CPU-bound program profiles itself (tests/profiler/cpu.ts); its pprof is decoded by hand
# (pprof.py) and checked against what the program measured: the split of the work between two functions, the
# process's CPU time, inlined and recursive frames, a method called from a closure.
cd "$(dirname "$0")/../.." || exit 1
X=./build/xisto; T=${TMPDIR:-/tmp}/xisto-profiler-test.$$; mkdir -p $T
trap 'rm -rf $T' EXIT
$X build -o $T/cpu tests/profiler/cpu.ts || { echo "FAIL build"; exit 1; }
$T/cpu $T/cpu.pprof > $T/out 2> $T/measured || { echo "FAIL run"; cat $T/out $T/measured; exit 1; }
printf 'started: true\nsecond start: false\nsamples > 0: true bytes > 0: true true\nempty after collect: true true\n' > $T/want
cmp -s $T/want $T/out || { echo "FAIL output"; diff $T/want $T/out; exit 1; }
set -- $(cat $T/measured)
python3 -I tests/profiler/pprof.py $T/cpu.pprof --check "$2" "$3" || { python3 -I tests/profiler/pprof.py $T/cpu.pprof | head -20; exit 1; }
echo "ok: profiler"
