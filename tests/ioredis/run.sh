#!/bin/sh
# packages/ioredis against a live server (docker: xisto-redis on 127.0.0.1:56379; database 3 is used and flushed)
# and a closed port (56398). expected.txt is the output of client.ts on Node with ioredis 5.11.1: commands, pipelines,
# server errors, a command timeout (another connection pauses the server), and connect()/commands with the server
# down (retry delays, maxRetriesPerRequest). Without a server the test is skipped, not failed.
cd "$(dirname "$0")" || exit 1
X=$(cd ../.. && pwd)/build/xisto
OUT=../../build/tests/ioredis
mkdir -p "$OUT/.xisto/pkgs"
ln -sfn "$(cd ../../packages/redis && pwd)" "$OUT/.xisto/pkgs/redis"
ln -sfn "$(cd ../../packages/ioredis && pwd)" "$OUT/.xisto/pkgs/ioredis"
cp client.ts "$OUT/client.ts"
if ! nc -z 127.0.0.1 56379 2>/dev/null; then echo "skip: no Redis on 127.0.0.1:56379 (docker start xisto-redis)"; exit 0; fi
XISTO_NODE_COMPAT=1 $X build -o "$OUT/client" "$OUT/client.ts" > "$OUT/build.txt" 2>&1 || { cat "$OUT/build.txt"; exit 1; }
XISTO_RC_STATS=1 "$OUT/client" > "$OUT/out.txt" 2> "$OUT/stats.txt"
if ! cmp -s expected.txt "$OUT/out.txt"; then echo "FAIL: output"; diff expected.txt "$OUT/out.txt"; exit 1; fi
if ! grep -q ' 0 still alive' "$OUT/stats.txt"; then echo "FAIL: leak: $(cat "$OUT/stats.txt")"; exit 1; fi
echo "ok: ioredis client (commands, pipelines, server errors, command timeout, server down, 0 alive)"
