#!/bin/sh
# The Conduit benchmark on a Linux machine without Docker (bench/conduit/run.py): a private Postgres cluster on
# port 55432 and a private Redis on 56390 (not the 56379 the test suite uses), and a copy of examples/conduit,
# next to it so that its package links still resolve, with the Redis port changed; then both builds.
# Usage (from the repository): sh bench/conduit/linux-setup.sh; then . ~/cbench/env.sh and run bench/conduit/run.py
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
W=$HOME/cbench
PGBIN=$(ls -d /usr/lib/postgresql/*/bin | sort -V | tail -1)
mkdir -p "$W"
if [ ! -d "$W/pg" ]; then
    "$PGBIN/initdb" -D "$W/pg" -U xisto --auth=trust --locale=C.UTF-8 > "$W/initdb.log"
fi
"$PGBIN/pg_ctl" -D "$W/pg" -o "-p 55432 -k /tmp -c listen_addresses=127.0.0.1" -l "$W/pg.log" status > /dev/null 2>&1 ||
    "$PGBIN/pg_ctl" -D "$W/pg" -o "-p 55432 -k /tmp -c listen_addresses=127.0.0.1" -l "$W/pg.log" -w start > /dev/null
PSQL="$PGBIN/psql -h 127.0.0.1 -p 55432 -U xisto"
$PSQL -d postgres -tc "SELECT 1 FROM pg_database WHERE datname = 'conduit'" | grep -q 1 || $PSQL -d postgres -qc "CREATE DATABASE conduit"
$PSQL -d postgres -qc "ALTER USER xisto PASSWORD 'xisto'"
$PSQL -d conduit -q -f examples/conduit/schema.sql
redis-cli -p 56390 ping > /dev/null 2>&1 || redis-server --port 56390 --save "" --appendonly no --daemonize yes > /dev/null
APP=$ROOT/examples/conduit-bench
# (without npm on the machine: copy examples/conduit/node_modules and dist from a machine that built them, first;
#  argon2 ships prebuilt binaries for Linux, the rest is JavaScript)
rm -rf "$APP" && cp -r examples/conduit "$APP" && rm -rf "$APP/.xisto" "$APP/conduit"
sed -i 's#redis://127.0.0.1:56379#redis://127.0.0.1:56390#' "$APP/src/config.ts" $(ls "$APP/dist/config.js" 2>/dev/null)
sed -i 's#examples/conduit#examples/conduit-bench#' "$APP/setup.sh" 2>/dev/null || true
(cd "$APP" && sh setup.sh > /dev/null && if command -v npm > /dev/null; then npm ci --no-audit --no-fund > npm.log 2>&1 && npm run build > /dev/null; else test -f dist/main.js -a -d node_modules; fi && "$ROOT/build/xisto" build -o conduit src/main.ts)
cat > "$W/env.sh" <<ENV
export CONDUIT_APP=$APP
export RESET_CMD="$PSQL -d conduit -qc 'TRUNCATE \\"user\\", \\"article\\", \\"comment\\", \\"follows\\", \\"tag\\", \\"user_favorites_article\\" RESTART IDENTITY CASCADE' && redis-cli -p 56390 FLUSHALL > /dev/null"
export FLUSH_CMD="redis-cli -p 56390 FLUSHALL"
export REDIS_STOP_CMD="redis-cli -p 56390 shutdown nosave || true"
export REDIS_START_CMD="redis-server --port 56390 --save '' --appendonly no --daemonize yes && sleep 0.3"
export LOAD=wrk
ENV
echo "env: . $W/env.sh"
