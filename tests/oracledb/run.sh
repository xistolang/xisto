#!/bin/sh
# packages/oracledb against a live Oracle Database: XISTO_ORACLE_URL=oracle://user:password@host:port/service
# (docker: gvenzl/oracle-free on 127.0.0.1:1521, service FREEPDB1). The test creates and drops table XISTO_ITEMS.
# Without the variable the test is skipped, not failed. expected.txt is what node-oracledb 6 thin prints for the
# same operations; the client runs in America/Sao_Paulo since DATE and TIMESTAMP are read in the local time zone.
cd "$(dirname "$0")" || exit 1
if [ -z "$XISTO_ORACLE_URL" ]; then echo "skip: XISTO_ORACLE_URL is not set (oracle://user:password@127.0.0.1:1521/FREEPDB1)"; exit 0; fi
X=$(cd ../.. && pwd)/build/xisto
OUT=../../build/tests/oracledb
mkdir -p "$OUT/.xisto/pkgs"
ln -sfn "$(cd ../../packages/oracledb && pwd)" "$OUT/.xisto/pkgs/oracledb"
cp client.ts "$OUT/client.ts"
XISTO_NODE_COMPAT=1 $X build -o "$OUT/client" "$OUT/client.ts" || exit 1
for mode in fast classic; do
    if [ $mode = classic ]; then NOFAST=1; else NOFAST=0; fi
    TZ=America/Sao_Paulo XISTO_ORACLE_NO_FAST_AUTH=$NOFAST XISTO_RC_STATS=1 "$OUT/client" > "$OUT/out.txt" 2> "$OUT/stats.txt"
    if ! cmp -s expected.txt "$OUT/out.txt"; then echo "FAIL: output ($mode login)"; diff expected.txt "$OUT/out.txt"; exit 1; fi
    if ! grep -q ' 0 still alive' "$OUT/stats.txt"; then echo "FAIL: leak ($mode login): $(cat "$OUT/stats.txt")"; exit 1; fi
done
echo "ok: oracledb client (fast and classic login, types, DML, binds, transactions, ORA errors, 250 rows, CLOB/BLOB, 1500 statements, pool, 0 alive)"
