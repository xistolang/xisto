#!/bin/sh
# Builds the example with xisto, runs the scenario and compares it with test/expected.txt: the output of the same
# source on Node with the real npm packages (@nestjs/*, class-validator, helmet, compression...).
set -u
cd "$(dirname "$0")/.." || exit 1
X=$(cd ../.. && pwd)/build/xisto
PORT=${PORT:-3199}
W=$(mktemp -d)
sh setup.sh
$X build -o "$W/app" src/main.ts > "$W/build.txt" 2>&1 || { echo "FAIL nest-versions: build"; cat "$W/build.txt"; rm -rf "$W"; exit 1; }
PORT=$PORT "$W/app" > "$W/server.txt" 2>&1 &
pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/api/v2/two" && break
  sleep 0.3
done
python3 test/scenario.py "http://127.0.0.1:$PORT" > "$W/out.txt"
kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
if diff test/expected.txt "$W/out.txt" > "$W/diff.txt"; then
  echo "ok: nest-versions serves the versions as Nest on Node ($(wc -l < test/expected.txt | tr -d ' ') requests)"
  rm -rf "$W"
else
  echo "FAIL nest-versions: differs from Node"
  cat "$W/diff.txt"
  rm -rf "$W"
  exit 1
fi
