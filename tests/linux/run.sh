#!/bin/sh
# Runs inside a Debian container: builds xisto, runs the suite against the oracle outputs produced on macOS
# (Node is not needed here), then drives the coroutine HTTP server on epoll against a slow fake database.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
export TZ=America/New_York # date-local checks DST edges: the same zone for xisto, Node and the Linux run
set -e
cd /work
make -s > /tmp/make.log 2>&1 || { cat /tmp/make.log; exit 1; }
echo "== $(uname -sm), $(cc --version | head -1)"
X=./build/xisto
OUT=/tmp/xt; mkdir -p $OUT
pass=0; fail=0; qbe=0
for f in tests/ok/*.ts; do
    name=$(basename "$f" .ts)
    oracle=oracle/$name.node
    [ -f "$oracle" ] || continue
    if ! $X build -o $OUT/$name "$f" > $OUT/$name.build 2>&1; then echo "FAIL build $f"; head -5 $OUT/$name.build; fail=$((fail+1)); continue; fi
    XISTO_RC_STATS=1 $OUT/$name > $OUT/$name.out 2> $OUT/$name.stats || true
    if ! grep -q ' 0 still alive' $OUT/$name.stats; then echo "FAIL leak $f: $(cat $OUT/$name.stats)"; fail=$((fail+1)); continue; fi
    if cmp -s "$oracle" $OUT/$name.out; then pass=$((pass+1)); else echo "FAIL output $f"; diff "$oracle" $OUT/$name.out | head -6; fail=$((fail+1)); fi
    if $X build --backend=qbe -o $OUT/$name-qbe "$f" > /dev/null 2>&1; then
        qbe=$((qbe+1))
        if $OUT/$name-qbe 2>/dev/null | cmp -s "$oracle" -; then pass=$((pass+1)); else echo "FAIL qbe $f"; fail=$((fail+1)); fi
    fi
done
for f in tests/err/*.ts; do
    want=$(sed -n '1s|^// expect: ||p' "$f")
    case "$want" in "runtime error"*) continue ;; esac
    if $X build -o $OUT/e "$f" > $OUT/e.txt 2>&1; then echo "FAIL $f compiled"; fail=$((fail+1)); continue; fi
    if grep -qF -- "$want" $OUT/e.txt; then pass=$((pass+1)); else echo "FAIL $f: $(head -1 $OUT/e.txt)"; fail=$((fail+1)); fi
done
for t in p256_comb p256_sign p256_montymul; do
    extra=""; [ $t = p256_sign ] && extra=tests/crypto/ecdsa_sign_orig.c
    [ $t = p256_montymul ] && [ "$(uname -m)" != aarch64 ] && continue
    if cc -O2 -Ibuild/bearssl-inc -Ithird_party/bearssl/inc -Ithird_party/bearssl/src -o $OUT/$t tests/crypto/$t.c $extra build/libbearssl.a && $OUT/$t 20000 > $OUT/$t.txt 2>&1; then pass=$((pass+1)); else echo "FAIL crypto $t"; cat $OUT/$t.txt; fail=$((fail+1)); fi
done
# programs the macOS suite also checks through qbe: a backend that stops building them must show up here
if [ $qbe -lt 12 ]; then echo "FAIL qbe: only $qbe programs built through qbe (expected 12)"; fail=$((fail+1)); fi
echo "linux tests: $pass passed, $fail failed ($qbe through qbe)"

# coroutine server on epoll: fake database answering after N ms (python asyncio), app from the deps bench
cat > /tmp/db.py <<'PYEOF'
import asyncio
async def handle(r, w):
    while True:
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = await r.read(4096)
            if not chunk: w.close(); return
            head += chunk
        line = head.split(b"\r\n", 1)[0].decode()
        ms = int(line.split("ms=")[1].split(" ")[0]) if "ms=" in line else 0
        await asyncio.sleep(ms / 1000)
        body = b'{"rows":[{"id":1,"name":"isa"},{"id":2,"name":"ju"}],"ms":%d}' % ms
        w.write(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n" % len(body) + body)
        await w.drain()
async def main():
    s = await asyncio.start_server(handle, "127.0.0.1", 8081, backlog=1024)
    async with s: await s.serve_forever()
asyncio.run(main())
PYEOF
python3 /tmp/db.py & DB=$!
$X build -o $OUT/app bench/http/deps/app.xisto.ts
$OUT/app > $OUT/app.log 2>&1 & APP=$!
sleep 1
echo "-- curl /three?ms=5: $(curl -s 'http://127.0.0.1:8080/three?ms=5')"
for ms in 1 100; do
    wrk -t2 -c64 -d4s "http://127.0.0.1:8080/one?ms=$ms" 2>&1 | awk -v ms=$ms '/Requests\/sec/{rps=$2} /Latency/{lat=$2} /Non-2xx|Socket errors/{err=$0} END{print "-- epoll one ms=" ms ": " rps " req/s, avg latency " lat (err ? ", " err : "")}'
done
echo "-- server RSS: $(ps -o rss= -p $APP) KB"
kill $APP $DB 2>/dev/null || true
# plain throughput on epoll
$X build -o $OUT/srv bench/http/server.xisto.ts
$OUT/srv > /dev/null 2>&1 & SRV=$!
sleep 0.5
wrk -t2 -c64 -d4s http://127.0.0.1:8080/ping 2>&1 | awk '/Requests\/sec/{print "-- epoll /ping: " $2 " req/s"}'
kill $SRV
# TLS on both backends: AWS-LC (default, TLS 1.3) and BearSSL (64-bit curves, getentropy on glibc)
for b in awslc bearssl; do
    if [ $b = awslc ] && [ ! -f build/awslc/lib/libssl.a ]; then echo "-- tls awslc: skipped (not built here: make TLS=bearssl)"; continue; fi
    want=TLSv1.3; [ $b = bearssl ] && want=TLSv1.2
    XISTO_TLS=$b sh tests/tls/run.sh > $OUT/tls-$b.txt 2>&1 || true
    if grep -q '200 {"secure":true,"name":"isa"}' $OUT/tls-$b.txt && grep -q "certificate is not trusted" $OUT/tls-$b.txt \
       && grep -q "stream matches file" $OUT/tls-$b.txt && grep -q "session resumed: True" $OUT/tls-$b.txt \
       && grep -q "renegotiation refused" $OUT/tls-$b.txt && grep -q "close_notify sent" $OUT/tls-$b.txt \
       && grep -q "default protocol: $want" $OUT/tls-$b.txt; then echo "-- tls $b: ok ($want)"; else echo "-- tls $b: FAIL"; cat $OUT/tls-$b.txt; fi
    $OUT/../xisto-tls-test/server > /dev/null 2>&1 & TS=$!
    sleep 0.5
    wrk -t2 -c64 -d4s "https://127.0.0.1:8443/hello?name=isa" 2>&1 | awk -v b=$b '/Requests\/sec/{print "-- epoll https keep-alive (" b "): " $2 " req/s"}'
    kill $TS 2>/dev/null || true; wait $TS 2>/dev/null || true
done
sh tests/tlsgate/run.sh > $OUT/tlsgate.txt 2>&1 && echo "-- tlsgate: ok" || { echo "-- tlsgate: FAIL"; cat $OUT/tlsgate.txt; }
# stack traces with gcc: ELF section, frame pointers, .cold partitions
sh tests/stack/run.sh > $OUT/stack.txt 2>&1 && grep -q 'boom: 500 {"error":"internal error"}' $OUT/stack.txt && grep -q "    at boom (tests/stack/server.ts:10:31)" $OUT/stack.txt \
  && echo "-- stack: ok" || { echo "-- stack: FAIL"; cat $OUT/stack.txt; }
