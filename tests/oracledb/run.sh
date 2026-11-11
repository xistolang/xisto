#!/bin/sh
# packages/oracledb against a live Oracle Database: XISTO_ORACLE_URL=oracle://user:password@host:port/service
# (docker: gvenzl/oracle-free on 127.0.0.1:1521, service FREEPDB1). The test creates and drops table XISTO_ITEMS.
# outbinds.ts: PL/SQL OUT and IN OUT binds (expected-outbinds.txt from tests/oracledb/outbinds.js on node-oracledb 6.10 thin).
# Without the variable the test is skipped, not failed. expected.txt is what node-oracledb 6 thin prints for the
# same operations; the client runs in America/Sao_Paulo since DATE and TIMESTAMP are read in the local time zone.
cd "$(dirname "$0")" || exit 1
# without a database: the login ciphers against known answers
mkdir -p ../../build/tests/oracledb
if ! cc -O1 -w -o ../../build/tests/oracledb/crypto-kat crypto-kat.c || ! ../../build/tests/oracledb/crypto-kat; then echo "FAIL: login ciphers"; exit 1; fi
echo "ok: login ciphers (DES, AES-128/192/256, MD5, 10g verifier) match the known answers"
# without a database: a pool that cannot connect (closed port), more requests than poolMax at once
X0=$(cd ../.. && pwd)/build/xisto
OUT0=../../build/tests/oracledb
mkdir -p "$OUT0/.xisto/pkgs"
ln -sfn "$(cd ../../packages/oracledb && pwd)" "$OUT0/.xisto/pkgs/oracledb"
cp pool-refused.ts "$OUT0/pool-refused.ts"
XISTO_NODE_COMPAT=1 $X0 build --no-warnings -o "$OUT0/pool-refused" "$OUT0/pool-refused.ts" || exit 1
if ! perl -e 'alarm 30; exec @ARGV' "$OUT0/pool-refused" > "$OUT0/pool-refused.txt" 2>&1 || ! cmp -s expected-pool-refused.txt "$OUT0/pool-refused.txt"; then
    echo "FAIL: pool with a closed port"; diff expected-pool-refused.txt "$OUT0/pool-refused.txt"; exit 1
fi
echo "ok: pool with a closed port (no request waits forever)"
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
cp outbinds.ts "$OUT/outbinds.ts"
XISTO_NODE_COMPAT=1 $X build -o "$OUT/outbinds" "$OUT/outbinds.ts" 2>/dev/null || { XISTO_NODE_COMPAT=1 $X build -o "$OUT/outbinds" "$OUT/outbinds.ts"; exit 1; }
for tz in America/Sao_Paulo; do
    TZ=$tz XISTO_RC_STATS=1 "$OUT/outbinds" > "$OUT/outbinds.txt" 2> "$OUT/outbinds-stats.txt"
    if ! cmp -s expected-outbinds.txt "$OUT/outbinds.txt"; then echo "FAIL: OUT binds ($tz)"; diff expected-outbinds.txt "$OUT/outbinds.txt"; exit 1; fi
    if ! grep -q ' 0 still alive' "$OUT/outbinds-stats.txt"; then echo "FAIL: leak (OUT binds): $(cat "$OUT/outbinds-stats.txt")"; exit 1; fi
done
cp pool-txn.ts "$OUT/pool-txn.ts"
XISTO_NODE_COMPAT=1 $X build --no-warnings -o "$OUT/pool-txn" "$OUT/pool-txn.ts" || exit 1
if ! "$OUT/pool-txn" > "$OUT/pool-txn.txt" 2>&1 || ! cmp -s expected-pool-txn.txt "$OUT/pool-txn.txt"; then
    echo "FAIL: pool release and open transactions"; diff expected-pool-txn.txt "$OUT/pool-txn.txt"; exit 1
fi
echo "ok: pool release rolls back only an open transaction (nothing uncommitted reaches the next borrower)"
echo "ok: oracledb client (fast and classic login, types, DML, binds, transactions, ORA errors, 250 rows, CLOB/BLOB, 1500 statements, pool, 0 alive; OUT/IN OUT binds by name and position, NUMBER/STRING/DATE/BOOLEAN/CLOB/BLOB, NULL, 32767 bytes, CLOB/BLOB temporary LOBs)"
