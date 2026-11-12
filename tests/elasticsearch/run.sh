#!/bin/sh
# packages/@elastic/elasticsearch against a fake cluster (server.mjs, Node): every call's request and the decoded
# responses, errors, ignore: [404], a retry, a timeout; no leak. Skipped (not failed) without Node.
cd "$(dirname "$0")" || exit 1
X=$(cd ../.. && pwd)/build/xisto
OUT=../../build/tests/elasticsearch
mkdir -p "$OUT/.xisto/pkgs/@elastic"
ln -sfn "$(cd ../../packages/@elastic/elasticsearch && pwd)" "$OUT/.xisto/pkgs/@elastic/elasticsearch"
cp client.ts "$OUT/client.ts"
if ! command -v node > /dev/null 2>&1; then echo "skip: no node for the fake cluster"; exit 0; fi
XISTO_NODE_COMPAT=1 XISTO_TSC_CHECKED=1 $X build --no-warnings -o "$OUT/client" "$OUT/client.ts" || exit 1
port=$((20000 + $$ % 20000))
node server.mjs $port > "$OUT/server.log" 2>&1 &
srv=$!
i=0
while [ $i -lt 50 ] && ! grep -q listening "$OUT/server.log" 2>/dev/null; do i=$((i + 1)); perl -e 'select(undef, undef, undef, 0.1)'; done
XISTO_RC_STATS=1 "$OUT/client" $port > "$OUT/out.txt" 2> "$OUT/stats.txt"
rc=$?
kill $srv 2>/dev/null; wait $srv 2>/dev/null
if [ $rc -ne 0 ]; then echo "FAIL: client exited $rc"; cat "$OUT/out.txt" "$OUT/stats.txt"; exit 1; fi
if ! cmp -s expected.txt "$OUT/out.txt"; then echo "FAIL: output"; diff expected.txt "$OUT/out.txt"; exit 1; fi
if ! grep -q ' 0 still alive' "$OUT/stats.txt"; then echo "FAIL: leak: $(cat "$OUT/stats.txt")"; exit 1; fi
echo "ok: elasticsearch client (18 calls, errors, ignore 404, retry, timeout, 0 alive)"
