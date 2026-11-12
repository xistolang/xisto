#!/bin/sh
# Builds the example with xisto, runs the scenario twice (pino lines, then ECS lines) and compares the answers and
# the controller's log lines with test/expected*.txt: the output of the same source on Node with Nest 11,
# class-validator 0.14, class-transformer 0.5, nestjs-pino 4 and @elastic/ecs-pino-format.
set -u
cd "$(dirname "$0")/.." || exit 1
X=$(cd ../.. && pwd)/build/xisto
PORT=${PORT:-3195}
W=$(mktemp -d)
sh setup.sh
XISTO_TSC_CHECKED=1 $X build -o "$W/app" src/main.ts > "$W/build.txt" 2>&1 || { echo "FAIL nest-forms: build"; cat "$W/build.txt"; rm -rf "$W"; exit 1; }
fail=0
for fmt in pino ecs; do
  LOG_FORMAT=$fmt FORMS_TTL_SEC=120 PORT=$PORT "$W/app" > "$W/server-$fmt.txt" 2>&1 &
  pid=$!
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    curl -s -o /dev/null "http://127.0.0.1:$PORT/api/forms/settings" && break
    sleep 0.3
  done
  python3 test/scenario.py "http://127.0.0.1:$PORT" > "$W/out-$fmt.txt"
  kill "$pid" 2>/dev/null
  wait "$pid" 2>/dev/null
  python3 test/logs.py "$W/server-$fmt.txt" > "$W/log-$fmt.txt"
  if ! diff test/expected.txt "$W/out-$fmt.txt"; then fail=1; fi
  exp=test/expected-log.txt
  [ "$fmt" = ecs ] && exp=test/expected-ecs-log.txt
  if ! diff "$exp" "$W/log-$fmt.txt"; then fail=1; fi
done
rm -rf "$W"
if [ $fail -eq 0 ]; then
  echo "ok: nest-forms answers and logs as Nest on Node ($(wc -l < test/expected.txt | tr -d ' ') requests, pino and ECS lines)"
else
  echo "FAIL nest-forms: differs from Node"
  exit 1
fi
