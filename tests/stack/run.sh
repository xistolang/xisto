#!/bin/sh
# Stack traces: exact output for runtime errors, uncaught throws and e.stack, on both backends where they
# apply (qbe has no try/catch), and the HTTP server's 500 (no details to the client, the trace on stderr).
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.." || exit 1
X=./build/xisto; T=${TMPDIR:-/tmp}/xisto-stack-test.$$; mkdir -p $T
fail=0
expect() { # name file-with-expected-output actual-output
    if cmp -s "$2" "$3"; then echo "ok   $1"; else echo "FAIL $1"; diff "$2" "$3" | head -20; fail=1; fi
}
for t in nested recursion uncaught catch; do
    $X build -o $T/$t tests/stack/$t.ts || { echo "FAIL build $t"; fail=1; continue; }
    $T/$t > $T/$t.out 2>&1; echo "exit $?" >> $T/$t.out
    expect "$t" tests/stack/$t.expected $T/$t.out
done
# every kind of frame: callbacks of map/filter/some/sort/forEach, field initializers, per-call-site copies
$X build -o $T/frames tests/stack/frames.ts && : > $T/frames.out
for c in filter some sort foreach method field-ctor field clone; do
    echo "== $c" >> $T/frames.out; $T/frames $c >> $T/frames.out 2>&1; echo "exit $?" >> $T/frames.out
done
expect frames tests/stack/frames.expected $T/frames.out
for t in nested recursion; do
    rm -rf tests/stack/.xisto
    $X build --backend=qbe -o $T/$t-qbe tests/stack/$t.ts || { echo "FAIL build qbe $t"; fail=1; continue; }
    $T/$t-qbe > $T/$t-qbe.out 2>&1; echo "exit $?" >> $T/$t-qbe.out
    expect "$t (qbe)" tests/stack/$t.expected $T/$t-qbe.out
done
rm -rf tests/stack/.xisto
# HTTP: a failing handler answers 500 {"error":"internal error"} and logs the message with its trace
$X build -o $T/server tests/stack/server.ts || exit 1
$T/server > $T/server.log 2>&1 & SRV=$!
python3 - <<'PY'
import socket, time, urllib.request, urllib.error, json
t0 = time.time()
while time.time() - t0 < 10:
    try: socket.create_connection(("127.0.0.1", 8091), timeout=0.2).close(); break
    except OSError: time.sleep(0.01)
def get(path, body=None):
    try:
        r = urllib.request.urlopen(urllib.request.Request("http://127.0.0.1:8091" + path, data=body))
        return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()
print("boom:", *get("/boom"))
print("late:", *get("/late"))
print("leak:", *get("/leak"))
s, b = get("/item", b'{"id": "x"}')
print("item:", s, json.loads(b))
PY
kill $SRV; wait $SRV 2>/dev/null
cat $T/server.log
rm -rf $T
exit $fail
