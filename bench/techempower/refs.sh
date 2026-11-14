#!/bin/sh
# TechEmpower references: builds the TFB's own implementations, unmodified except for the port (8080 -> 91xx, so
# they do not collide with the xisto test suite) and with no Docker: each is built and run directly on the host.
#
# Usage: bench/techempower/refs.sh [TFB_DIR] [OUT_DIR]
#   TFB_DIR  a clone of https://github.com/TechEmpower/FrameworkBenchmarks
#            (default ~/workspace/tessarotto/port/FrameworkBenchmarks; phase 1 used commit 57d92fbec6f8)
#   OUT_DIR  where the patched copies are built (default $TFB_DIR/../tfb-refs)
#
# References and ports (the same table is in run.py):
#   actix   frameworks/Rust/actix, bin tfb-web (actix-web), the TFB Dockerfile's RUSTFLAGS=-C target-cpu=native  9110
#   go-std  frameworks/Go/go-std (net/http of the standard library), `./app` as the go.dockerfile, -bind :9111     9111
#   nodejs  frameworks/JavaScript/nodejs (node:http, node:cluster one worker per CPU)                            9112
#   fastify frameworks/JavaScript/fastify (cluster, one worker per CPU)                                            9113
#   bun     frameworks/TypeScript/bun (Bun.serve, reusePort, spawn.ts starts one process per CPU)                  9114
# Toolchains: Rust via `rustup run stable cargo`, Go from /opt/homebrew/bin/go (or PATH), node/npm and bun on PATH.
set -e
TFB=${1:-$HOME/workspace/tessarotto/port/FrameworkBenchmarks}
OUT=${2:-$(dirname "$TFB")/tfb-refs}
F=$TFB/frameworks
mkdir -p "$OUT"
GO=$(command -v go || echo /opt/homebrew/bin/go)
command -v cargo >/dev/null 2>&1 || PATH=$(dirname "$(rustup which --toolchain stable cargo)"):$PATH
CARGO=cargo
only=${REFS:-"actix go-std nodejs fastify bun"}

copy() { rm -rf "${OUT:?}/${1:?}"; cp -R "$F/$2" "$OUT/$1"; }

for r in $only; do
  echo "== $r"
  case $r in
  actix)
    copy actix Rust/actix
    sed -i.bak 's/0\.0\.0\.0:8080/0.0.0.0:9110/' "$OUT/actix/src/main_web.rs"
    (cd "$OUT/actix" && RUSTFLAGS="-C target-cpu=native" $CARGO build --release --bin tfb-web) ;;
  go-std)
    copy go-std Go/go-std
    (cd "$OUT/go-std/src" && "$GO" mod download && "$GO" generate ./... && "$GO" build -ldflags="-s -w" -o app .) ;;
  nodejs)
    copy nodejs JavaScript/nodejs
    sed -i.bak 's/listen(8080/listen(9112/' "$OUT/nodejs/create-server.js"
    (cd "$OUT/nodejs" && npm install --silent --no-audit --no-fund) ;;
  fastify)
    copy fastify JavaScript/fastify
    sed -i.bak 's/port: 8080/port: 9113/' "$OUT/fastify/create-server.js"
    (cd "$OUT/fastify" && npm install --silent --no-audit --no-fund) ;;
  bun)
    copy bun TypeScript/bun
    sed -i.bak 's/port: 8080/port: 9114/' "$OUT/bun/src/index.ts"
    (cd "$OUT/bun/src" && NODE_ENV=production bun build --compile --minify --outfile server .) ;;
  esac
done
echo "refs built in $OUT"
