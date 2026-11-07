#!/bin/sh
# A branch against main on another machine, before merging: both trees are copied there, xisto is built from
# each, and each program runs RUNS times under both builds (alternating), plus a reference binary when one is
# given; outputs are checked to be the same. Prints the median wall time and CPU of each.
# Usage: KEY=key.pem sh bench/vm-ab.sh user@host MAIN_DIR BRANCH_DIR "PROGRAM ARG [< INPUT]"... [-- REF_DIR]
#   PROGRAM is a path under the repo (bench/benchmarksgame/x.ts); INPUT a file under the repo's bench/benchmarksgame/out
#   (copied once); REF_DIR a directory on the host with reference binaries named like the program (rust: ~/rustbg/...)
set -e
H=$1; MAIN=$2; BR=$3; shift 3
SSH="ssh -i $KEY -o BatchMode=yes $H"
AB=ab-$$ # (a directory per run on the host: two comparisons at once stay apart)
for side in main branch; do
    d=$MAIN; [ $side = branch ] && d=$BR
    (cd "$d" && COPYFILE_DISABLE=1 tar --exclude=./build --exclude=./bench/out --exclude=./bench/benchmarksgame/out --exclude=./.xisto \
        --exclude=./.claude --exclude='*.o' --exclude=./third_party/aws-lc --exclude=./node_modules -czf /tmp/ab-$side.tgz .)
    scp -i $KEY -o BatchMode=yes -q /tmp/ab-$side.tgz $H:$AB-$side.tgz
    $SSH "rm -rf ~/$AB/$side && mkdir -p ~/$AB/$side && tar -xzf ~/$AB-$side.tgz -C ~/$AB/$side 2>/dev/null && rm ~/$AB-$side.tgz && cd ~/$AB/$side && make -s TLS=bearssl > /tmp/$AB-$side-make.log 2>&1 && echo built $side"
done
for spec in "$@"; do
    [ "$spec" = "--" ] && break
    $SSH "cd ~/$AB && python3 - '$spec'" <<'PY'
import os, statistics, subprocess, sys, time, hashlib
spec = sys.argv[1]
prog, rest = spec.split(" ", 1)
arg, stdin = (rest.split(" < ") + [None])[:2]
env = dict(os.environ, XISTO_NODE_COMPAT="1", XISTO_CPU=os.environ.get("XISTO_CPU", ""))
exes = {}
for side in ("main", "branch"):
    exe = f"{os.getcwd()}/{side}-{os.path.basename(prog)[:-3]}"
    subprocess.run([f"{side}/build/xisto", "build", "-o", exe, f"{side}/{prog}"], env=env, check=True, stdout=subprocess.DEVNULL)
    exes[side] = exe
inp = os.path.expanduser(f"~/ab/inputs/{stdin}") if stdin else None  # (made once, outside any suite run)
res = {k: [] for k in exes}
outs = {k: set() for k in exes}
for _ in range(3):
    for k, exe in exes.items():
        f = open(inp, "rb") if inp else subprocess.DEVNULL
        t0 = time.perf_counter()
        p = subprocess.run([exe, arg], stdin=f, capture_output=True)
        w = time.perf_counter() - t0
        outs[k].add(hashlib.sha256(p.stdout).hexdigest())
        res[k].append(w)
same = "same" if len(outs["main"] | outs["branch"]) == 1 else "OUTPUTS DIFFER"
m, b = statistics.median(res["main"]), statistics.median(res["branch"])
print(f"{prog} {arg}: main {m:.3f}s  branch {b:.3f}s  ({m / b:.2f}x)  {same}")
PY
done
$SSH "rm -rf ~/$AB" # (the inputs in ~/ab/inputs stay)
