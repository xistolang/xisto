#!/bin/sh
# Benchmark bench: runs each program on Node, Bun, Xisto (dev) and Xisto (release),
# records median wall time, peak RSS and executable size into bench/results/.
# Usage: bench/run.sh [RUNS]   (default 5)
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/.." || exit 1
RUNS=${1:-5}
XISTO=./build/xisto
OUT=bench/out
mkdir -p "$OUT" bench/results
stamp=$(date +%Y-%m-%d)
host=$(sysctl -n machdep.cpu.brand_string 2>/dev/null | tr ' ' '-' | tr -cd 'A-Za-z0-9-')
csv="bench/results/$stamp-$host.csv"
md="bench/results/$stamp-$host.md"

NODE=$(command -v node)
BUN=$(command -v bun)
[ -n "$NODE" ] || { echo "node not found"; exit 1; }

echo "program,target,runs,time_median_s,time_min_s,max_rss_mb,executable_bytes,output_ok,compile_s" > "$csv"
{
    echo "# Bench $stamp"
    echo
    echo "Machine: $(sysctl -n machdep.cpu.brand_string), $(sysctl -n hw.memsize | awk '{printf "%d GB", $1/1073741824}') RAM, macOS $(sw_vers -productVersion)"
    echo "Runtimes: node $($NODE --version), bun $([ -n "$BUN" ] && $BUN --version || echo 'n/a'), $(cc --version | head -1)"
    echo "Runs per cell: $RUNS (median wall time; min in CSV). Executable = what you have to ship to run the program. Compile = median of 3 runs of xisto build (xisto-dev and xisto-rel go through cc; xisto-qbe goes through qbe + as + ld, no C)."
    echo
    echo "| program | target | time (s) | peak RSS (MB) | executable (MB) | compile (s) | output |"
    echo "|---|---|---:|---:|---:|---:|---|"
} > "$md"

ship_size() { # bytes you must ship to run the program: the executable plus any private runtime library it links
    exe=$(realpath "$1")
    total=$(stat -Lf%z "$exe")
    for lib in $(otool -L "$exe" 2>/dev/null | awk '/@rpath\/lib(node|bun)/{print $1}' | sed 's|@rpath/||'); do
        f="$(dirname "$exe")/../lib/$lib"
        [ -f "$f" ] && total=$((total + $(stat -Lf%z "$f")))
    done
    echo "$total"
}

measure() { # $1=program $2=target $3...=command ; prints "median min rss"
    prog=$1; target=$2; shift 2
    times=""
    rss=0
    i=0
    while [ $i -lt "$RUNS" ]; do
        /usr/bin/time -l "$@" > /dev/null 2> "$OUT/time.txt"
        t=$(awk '/real/{print $1}' "$OUT/time.txt")
        r=$(awk '/maximum resident set size/{print $1}' "$OUT/time.txt")
        times="$times $t"
        [ "$r" -gt "$rss" ] && rss=$r
        i=$((i+1))
    done
    median=$(echo "$times" | tr ' ' '\n' | grep . | sort -n | awk '{a[NR]=$1} END{print a[int((NR+1)/2)]}')
    min=$(echo "$times" | tr ' ' '\n' | grep . | sort -n | head -1)
    echo "$median $min $rss"
}

for src in bench/programs/*.ts; do
    prog=$(basename "$src" .ts)
    echo "== $prog"
    "$NODE" "$src" > "$OUT/$prog.expected" 2>&1

    # build xisto variants, timing the compiler itself (median of 3)
    ctime() { # prints median wall seconds of 3 runs of the command
        ts=""
        for k in 1 2 3; do
            /usr/bin/time -p "$@" 2> "$OUT/ctime.txt" > /dev/null || { cat "$OUT/ctime.txt"; exit 1; }
            ts="$ts $(awk '/^real/{print $2}' "$OUT/ctime.txt")"
        done
        echo "$ts" | tr ' ' '\n' | grep . | sort -n | sed -n 2p
    }
    c_dev=$(ctime $XISTO build --dev -o "$OUT/$prog-dev" "$src")
    c_rel=$(ctime $XISTO build -o "$OUT/$prog-rel" "$src")
    c_qbe=$(ctime $XISTO build --backend=qbe -o "$OUT/$prog-qbe" "$src")
    # first run of a fresh binary pays macOS code-signing verification; warm it up
    "$OUT/$prog-dev" > /dev/null; "$OUT/$prog-rel" > /dev/null; "$OUT/$prog-qbe" > /dev/null

    run_target() { # $1=target $2=exe_for_size $3=compile_time $4...=cmd
        target=$1; exe=$2; ctime_s=$3; shift 3
        "$@" > "$OUT/$prog.$target.out" 2>&1
        if cmp -s "$OUT/$prog.expected" "$OUT/$prog.$target.out"; then ok=yes; else ok=NO; fi
        set -- $(measure "$prog" "$target" "$@")
        size=$(ship_size "$exe")
        printf "%s,%s,%s,%s,%s,%.1f,%s,%s,%s\n" "$prog" "$target" "$RUNS" "$1" "$2" "$(echo "$3" | awk '{print $1/1048576}')" "$size" "$ok" "$ctime_s" >> "$csv"
        printf "| %s | %s | %s | %.1f | %.2f | %s | %s |\n" "$prog" "$target" "$1" "$(echo "$3" | awk '{print $1/1048576}')" "$(echo "$size" | awk '{print $1/1048576}')" "$ctime_s" "$ok" >> "$md"
        printf "   %-14s %6ss  %7.1f MB rss  output=%s\n" "$target" "$1" "$(echo "$3" | awk '{print $1/1048576}')" "$ok"
    }

    run_target node "$NODE" - "$NODE" "$src"
    [ -n "$BUN" ] && run_target bun "$BUN" - "$BUN" "$src"
    run_target xisto-dev "$OUT/$prog-dev" "$c_dev" "$OUT/$prog-dev"
    run_target xisto-rel "$OUT/$prog-rel" "$c_rel" "$OUT/$prog-rel"
    run_target xisto-qbe "$OUT/$prog-qbe" "$c_qbe" "$OUT/$prog-qbe"
done

echo
echo "results: $csv"
echo "         $md"
