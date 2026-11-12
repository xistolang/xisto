#!/bin/sh
# packages/mongodb: BSON with no server (bson.ts against expected-bson.txt, which is bson.js on Node with js-bson
# 6.10: the bytes of what a program writes, and server documents read back as JSON). The driver against a live
# server is tested through mongoose (tests/mongoose, XISTO_MONGO_URL).
cd "$(dirname "$0")" || exit 1
X=$(cd ../.. && pwd)/build/xisto
OUT=../../build/tests/mongodb
mkdir -p "$OUT/.xisto/pkgs"
ln -sfn "$(cd ../../packages/mongodb && pwd)" "$OUT/.xisto/pkgs/mongodb"
cp bson.ts "$OUT/bson.ts"
$X build -o "$OUT/bson" "$OUT/bson.ts" > "$OUT/bson.build.txt" 2>&1 || { echo "FAIL: build bson"; cat "$OUT/bson.build.txt"; exit 1; }
XISTO_RC_STATS=1 "$OUT/bson" > "$OUT/bson.txt" 2> "$OUT/bson.stats.txt"
if ! cmp -s expected-bson.txt "$OUT/bson.txt"; then echo "FAIL: bson output"; diff expected-bson.txt "$OUT/bson.txt"; exit 1; fi
if ! grep -q ' 0 still alive' "$OUT/bson.stats.txt"; then echo "FAIL: bson leak: $(cat "$OUT/bson.stats.txt")"; exit 1; fi
echo "ok: mongodb BSON (writes as js-bson 6, reads as Node: int32/double, int64, decimal128, dates, UTF-8)"
