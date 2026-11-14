#!/bin/sh
# TechEmpower round for the official Linux ruler (phase 1: plaintext and json; the database tests come in phase 2).
# Prepared, not yet run on the VMs. Runs on the SERVER machine from the repository root:
#
#   sh bench/techempower/vm-round.sh                                   # generator on the same machine
#   GEN_SSH=ubuntu@10.0.0.12 SERVER_HOST=10.0.0.11 sh bench/techempower/vm-round.sh   # generator on another one
#
# Steps: checks the tools; clones the TFB at the pinned commit (TFB_DIR); builds xisto (make), the references
# (refs.sh) and the xisto apps; then run.py with wrk, the TFB's levels and its pipelining, REPEAT runs each, the
# median. Results: bench/results/techempower-<date>/{json,plaintext}.txt and env.txt (machine, versions, commits).
#
# Variables (defaults in brackets):
#   TFB_DIR [~/tfb/FrameworkBenchmarks]  TFB_COMMIT [57d92fbec6f8fd7431bc77326dd0484e60c96e20]
#   REFS_DIR [$TFB_DIR/../tfb-refs]      DURATION [15] (the TFB uses 15 s)   REPEAT [3]
#   LEVELS [16,32,64,128,256,512] (json)  PIPE_LEVELS [256,1024,4096,16384] (plaintext, pipeline depth 16)
#   SERVERS [xisto-native,xisto-nest,actix,go-std,nodejs,fastify,bun]  XISTO_WORKERS [nproc]
#   GEN_SSH (user@host of the generator: needs wrk there and key-based SSH)  SERVER_HOST (this machine, seen from it)
# Needs here: cc, make, cmake, python3, git, rustup (stable), go, node+npm, bun; wrk here or on the generator.
# Raise the open-file limit for 16384 connections (ulimit -n 65536, on both machines).
set -e
cd "$(dirname "$0")/../.." || exit 1
TFB_DIR=${TFB_DIR:-$HOME/tfb/FrameworkBenchmarks}
TFB_COMMIT=${TFB_COMMIT:-57d92fbec6f8fd7431bc77326dd0484e60c96e20}
REFS_DIR=${REFS_DIR:-$(dirname "$TFB_DIR")/tfb-refs}
DURATION=${DURATION:-15}
REPEAT=${REPEAT:-3}
LEVELS=${LEVELS:-16,32,64,128,256,512}
PIPE_LEVELS=${PIPE_LEVELS:-256,1024,4096,16384}
SERVERS=${SERVERS:-xisto-native,xisto-nest,actix,go-std,nodejs,fastify,bun}
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
XISTO_WORKERS=${XISTO_WORKERS:-$NPROC}
OUT=bench/results/techempower-$(date +%Y-%m-%d)
mkdir -p "$OUT"
# shellcheck disable=SC3045
ulimit -n 65536 2>/dev/null || echo "warning: could not raise the open-file limit ($(ulimit -n))"

missing=""
for t in cc make cmake python3 git rustup go node npm bun; do command -v $t > /dev/null 2>&1 || missing="$missing $t"; done
if [ -n "$GEN_SSH" ]; then
    [ -n "$SERVER_HOST" ] || { echo "GEN_SSH needs SERVER_HOST (this machine's address as the generator sees it)"; exit 1; }
    ssh "$GEN_SSH" 'command -v wrk' > /dev/null || missing="$missing wrk(on $GEN_SSH)"
else
    command -v wrk > /dev/null 2>&1 || missing="$missing wrk"
fi
[ -z "$missing" ] || { echo "missing:$missing"; exit 1; }

if [ ! -d "$TFB_DIR/.git" ]; then
    mkdir -p "$(dirname "$TFB_DIR")"
    git clone --filter=blob:none https://github.com/TechEmpower/FrameworkBenchmarks.git "$TFB_DIR"
fi
(cd "$TFB_DIR" && git fetch -q --depth 1 origin "$TFB_COMMIT" 2>/dev/null; git checkout -q "$TFB_COMMIT")

make -j"$NPROC"
sh bench/techempower/refs.sh "$TFB_DIR" "$REFS_DIR"

LUA="$TFB_DIR/toolset/wrk/pipeline.lua"
GEN_ARGS=""
if [ -n "$GEN_SSH" ]; then
    scp -q "$LUA" "$GEN_SSH:/tmp/tfb-pipeline.lua"
    LUA=/tmp/tfb-pipeline.lua
    GEN_ARGS="--gen-ssh $GEN_SSH --server-host $SERVER_HOST --gen-threads $(ssh "$GEN_SSH" nproc)"
fi

{
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host: $(uname -a)"
    echo "cpu: $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2) ($NPROC cpus)"
    echo "xisto: $(git rev-parse --short HEAD) $(./build/xisto --version)"
    echo "tfb: $TFB_COMMIT"
    echo "rust: $(rustup run stable rustc --version)  go: $(go version)  node: $(node --version)  bun: $(bun --version)"
    echo "generator: ${GEN_SSH:-local}  wrk: $(${GEN_SSH:+ssh $GEN_SSH} wrk --version 2>&1 | head -1)"
    echo "duration: ${DURATION}s  repeat: $REPEAT (median)  xisto workers: $XISTO_WORKERS"
} > "$OUT/env.txt"
cat "$OUT/env.txt"

# shellcheck disable=SC2086
python3 bench/techempower/run.py --gen wrk --tests json --levels "$LEVELS" --duration "$DURATION" --repeat "$REPEAT" \
    --servers "$SERVERS" --xisto-workers "$XISTO_WORKERS" --refs "$REFS_DIR" --tfb "$TFB_DIR" $GEN_ARGS \
    --out "$OUT/json.txt"
# shellcheck disable=SC2086
python3 bench/techempower/run.py --gen wrk --tests plaintext --levels "$PIPE_LEVELS" --duration "$DURATION" \
    --repeat "$REPEAT" --servers "$SERVERS" --xisto-workers "$XISTO_WORKERS" --refs "$REFS_DIR" --tfb "$TFB_DIR" \
    --pipeline-lua "$LUA" --no-build $GEN_ARGS --out "$OUT/plaintext.txt"
echo "results in $OUT"
