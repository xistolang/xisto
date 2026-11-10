#!/bin/sh
# Test runner: programs in tests/ok must print exactly what Node prints;
# programs in tests/err must fail to compile with the message in their "// expect:" line.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
export TZ=America/New_York # date-local checks DST edges: the same zone for xisto, Node and the Linux run
cd "$(dirname "$0")/.." || exit 1
XISTO=./build/xisto
OUT=build/tests
mkdir -p "$OUT"
pass=0
fail=0
qbe=0

for f in tests/ok/*.ts; do
    name=$(basename "$f" .ts)
    in=/dev/null; [ -f "${f%.ts}.stdin" ] && in="${f%.ts}.stdin"   # NAME.stdin next to the program: its standard input, for Node and for xisto
    # oracle: Node; Bun when Node cannot run the file (Node strips types but does not transform enums)
    if [ -f "${f%.ts}.expected" ]; then
        cp "${f%.ts}.expected" "$OUT/$name.node"   # APIs only Xisto has (streams): the expected output is checked in
    elif [ -n "$XISTO_ORACLE_DIR" ] && [ -f "$XISTO_ORACLE_DIR/$name.node" ]; then
        cp "$XISTO_ORACLE_DIR/$name.node" "$OUT/$name.node"   # Node's outputs from another machine (a system whose Node cannot run .ts)
    elif ! node --import ./packages/xisto/register.mjs "$f" < "$in" > "$OUT/$name.node" 2>&1; then   # (xisto:io, xisto:bytes: the reference implementations)
        if ! bun "$f" < "$in" > "$OUT/$name.node" 2>&1; then
            echo "FAIL $f: neither node nor bun can run the program"; cat "$OUT/$name.node" | head -5; fail=$((fail+1)); continue
        fi
    fi
    bflags=$(sed -n 's|^// build: ||p' "$f")   # "// build: --flag": options for this program's builds
    if ! $XISTO build $bflags -o "$OUT/$name" "$f" > "$OUT/$name.build" 2>&1; then
        echo "FAIL $f: xisto build failed"; cat "$OUT/$name.build"; fail=$((fail+1)); continue
    fi
    # "// warns: TEXT" lines: each TEXT must be among the build's warnings; "// warns: none": no warning at all
    wfail=
    while IFS= read -r w; do
        if [ -z "$w" ]; then :
        elif [ "$w" = "none" ]; then grep -q "warning" "$OUT/$name.build" && wfail="no warning expected, got: $(grep warning "$OUT/$name.build" | head -3)"
        elif ! grep -qF -- "$w" "$OUT/$name.build"; then wfail="missing warning: $w"; fi
    done <<EOF_WARNS
$(sed -n 's|^// warns: ||p' "$f")
EOF_WARNS
    if [ -n "$wfail" ]; then echo "FAIL $f: $wfail"; fail=$((fail+1)); continue; fi
    XISTO_RC_STATS=1 "$OUT/$name" < "$in" > "$OUT/$name.xisto" 2> "$OUT/$name.stats"
    if ! grep -q ' 0 still alive' "$OUT/$name.stats"; then
        echo "FAIL $f: leak: $(cat "$OUT/$name.stats")"; fail=$((fail+1)); continue
    fi
    if cmp -s "$OUT/$name.node" "$OUT/$name.xisto"; then
        pass=$((pass+1))
    else
        echo "FAIL $f: output differs from node"
        diff "$OUT/$name.node" "$OUT/$name.xisto" | head -20
        fail=$((fail+1))
    fi
    # the same program through the QBE backend, when it is inside the subset that backend covers
    if $XISTO build $bflags --backend=qbe -o "$OUT/$name-qbe" "$f" > "$OUT/$name.qbe.build" 2>&1; then
        XISTO_RC_STATS=1 "$OUT/$name-qbe" < "$in" > "$OUT/$name.qbe" 2> "$OUT/$name.qbe.stats"
        if ! grep -q ' 0 still alive' "$OUT/$name.qbe.stats"; then
            echo "FAIL $f (qbe): leak: $(cat "$OUT/$name.qbe.stats")"; fail=$((fail+1))
        elif cmp -s "$OUT/$name.node" "$OUT/$name.qbe"; then
            pass=$((pass+1)); qbe=$((qbe+1))
        else
            echo "FAIL $f (qbe): output differs from node"; diff "$OUT/$name.node" "$OUT/$name.qbe" | head -20; fail=$((fail+1))
        fi
    elif ! grep -q 'not supported by the qbe backend' "$OUT/$name.qbe.build"; then
        echo "FAIL $f (qbe): build failed"; cat "$OUT/$name.qbe.build"; fail=$((fail+1))
    fi
done

for f in tests/err/*.ts; do
    name=$(basename "$f" .ts)
    want=$(sed -n '1s|^// expect: ||p' "$f")
    case "$want" in
    "runtime error"*)
        if ! $XISTO build -o "$OUT/$name" "$f" > "$OUT/$name.err" 2>&1; then
            echo "FAIL $f: should compile"; cat "$OUT/$name.err"; fail=$((fail+1)); continue
        fi
        if "$OUT/$name" > "$OUT/$name.err" 2>&1; then
            echo "FAIL $f: ran successfully, but should have failed at runtime"; fail=$((fail+1)); continue
        fi
        want=${want#runtime error: }
        ;;
    *)
        if $XISTO build -o "$OUT/$name" "$f" > "$OUT/$name.err" 2>&1; then
            echo "FAIL $f: compiled, but should have been rejected"; fail=$((fail+1)); continue
        fi
        ;;
    esac
    if grep -qF -- "$want" "$OUT/$name.err"; then
        pass=$((pass+1))
    else
        echo "FAIL $f: expected message '$want', got:"; cat "$OUT/$name.err"; fail=$((fail+1))
    fi
done

# xisto check: each folder of tests/check is a small project; its summary, its --details and its exit status are
# checked in (expected.summary, expected.details, expected.rc)
for d in tests/check/*/; do
    d=${d%/}
    name=check-$(basename "$d")
    $XISTO check "$d" > "$OUT/$name.summary" 2>&1; rc=$?
    $XISTO check --details "$d" > "$OUT/$name.details" 2>&1
    # (a folder with an `entry` file: also checked from that .ts file, whose report counts the packages the libs it
    # reaches import: expected.entry)
    if [ -f "$d/entry" ]; then $XISTO check "$d/$(cat "$d/entry")" > "$OUT/$name.entry" 2>&1; else : > "$OUT/$name.entry"; fi
    if [ "$rc" = "$(cat "$d/expected.rc")" ] && cmp -s "$OUT/$name.summary" "$d/expected.summary" && cmp -s "$OUT/$name.details" "$d/expected.details" &&
       { [ ! -f "$d/entry" ] || cmp -s "$OUT/$name.entry" "$d/expected.entry"; }; then
        pass=$((pass+1))
    else
        echo "FAIL $d: xisto check (exit $rc)"; diff "$d/expected.summary" "$OUT/$name.summary" | head; diff "$d/expected.details" "$OUT/$name.details" | head
        [ -f "$d/entry" ] && diff "$d/expected.entry" "$OUT/$name.entry" | head; fail=$((fail+1))
    fi
done
# rule warnings (XB-118): a build prints one line with their count and still builds; --deny-warnings makes them
# errors in the build and changes in xisto check
$XISTO build tests/check/warnings/main.ts -o "$OUT/warnings-bin" > "$OUT/warnings-build.txt" 2>&1; wrc=$?
$XISTO build --deny-warnings tests/check/warnings/main.ts -o "$OUT/warnings-deny" > /dev/null 2>&1; drc=$?
$XISTO check --deny-warnings tests/check/warnings > /dev/null 2>&1; crc=$?
if [ "$wrc" = 0 ] && [ "$drc" = 1 ] && [ "$crc" = 1 ] && [ "$(wc -l < "$OUT/warnings-build.txt")" -eq 1 ] && grep -q '^xisto: 5 warnings (XS2101 1' "$OUT/warnings-build.txt"; then
    pass=$((pass+1))
else
    echo "FAIL rule warnings: build rc=$wrc, --deny-warnings rc=$drc, check --deny-warnings rc=$crc"; cat "$OUT/warnings-build.txt" | head -5; fail=$((fail+1))
fi

# stderr: console.error/warn go to stderr and log/info/debug to stdout, each as Node writes it (the expected files are
# Node's two streams, checked in: the tests/ok oracle merges them)
for f in tests/stderr/*.ts; do
    name=stderr-$(basename "$f" .ts)
    if ! $XISTO build -o "$OUT/$name" "$f" > "$OUT/$name.build" 2>&1; then echo "FAIL $f: xisto build failed"; cat "$OUT/$name.build"; fail=$((fail+1)); continue; fi
    "$OUT/$name" > "$OUT/$name.out" 2> "$OUT/$name.err" < /dev/null
    if cmp -s "$OUT/$name.out" "${f%.ts}.stdout" && cmp -s "$OUT/$name.err" "${f%.ts}.stderr"; then pass=$((pass+1));
    else echo "FAIL $f: stdout or stderr differs from node"; diff "${f%.ts}.stdout" "$OUT/$name.out" | head; diff "${f%.ts}.stderr" "$OUT/$name.err" | head; fail=$((fail+1)); fi
done

# xisto fmt: the messy file formats to the gabarito, and the whole tree is already formatted
cp tests/fmt/messy.input "$OUT/messy.ts"
$XISTO fmt "$OUT/messy.ts" > /dev/null 2>&1
if cmp -s "$OUT/messy.ts" tests/fmt/messy.expected && $XISTO fmt --check "$OUT/messy.ts" > /dev/null 2>&1; then pass=$((pass+1)); else echo "FAIL fmt: messy.input does not format to messy.expected"; diff tests/fmt/messy.expected "$OUT/messy.ts" | head; fail=$((fail+1)); fi
if $XISTO fmt --check $(find tests bench/programs bench/http bench/streams -name '*.ts') > "$OUT/fmt-check.txt" 2>&1; then pass=$((pass+1)); else echo "FAIL fmt --check:"; head "$OUT/fmt-check.txt"; fail=$((fail+1)); fi
# xisto test: the unit tests pass; a failing test is reported as a failure
if $XISTO test tests/unit > "$OUT/unit.txt" 2>&1; then pass=$((pass+1)); else echo "FAIL xisto test tests/unit:"; cat "$OUT/unit.txt"; fail=$((fail+1)); fi
mkdir -p "$OUT/unit-fail" && cp tests/unit/failing.test.ts.disabled "$OUT/unit-fail/failing.test.ts"
if $XISTO test "$OUT/unit-fail" > "$OUT/unit-fail.txt" 2>&1; then echo "FAIL xisto test: a failing assert was reported as passing"; fail=$((fail+1));
elif grep -q "assertion failed: math is broken" "$OUT/unit-fail.txt"; then pass=$((pass+1)); else echo "FAIL xisto test: no assertion message"; cat "$OUT/unit-fail.txt"; fail=$((fail+1)); fi

# HTTP: fetch options and timeout, non-blocking sleeps, graceful shutdown (SKIP_NET=1 skips: it needs port 8080)
if [ -n "$SKIP_NET" ]; then :; else
sh tests/http/run.sh > "$OUT/http.txt" 2>&1
if grep -q "200 POST abc123 7 xisto" "$OUT/http.txt" && grep -q "timeout: The operation was aborted due to timeout" "$OUT/http.txt" && grep -q "200 slept 20" "$OUT/http.txt" \
   && grep -q "overlapped (20 ok)" "$OUT/http.txt" && grep -q "during SIGTERM: slept 700" "$OUT/http.txt" && grep -q "refused: True" "$OUT/http.txt" \
   && grep -q "server exit code: 0" "$OUT/http.txt" && grep -q "channel delivered: True" "$OUT/http.txt" \
   && grep -q "pipelined: 3 responses" "$OUT/http.txt" && grep -q "async local storage per request: isolated (30 ok)" "$OUT/http.txt" && grep -q "parked client left: idle cpu" "$OUT/http.txt"; then pass=$((pass+1)); else echo "FAIL http:"; cat "$OUT/http.txt"; fail=$((fail+1)); fi
# crypto patches to BearSSL (third_party/bearssl/XISTO_PATCHES.md): differential tests
for t in p256_comb p256_sign p256_montymul; do
    extra=""; [ $t = p256_sign ] && extra=tests/crypto/ecdsa_sign_orig.c
    # p256_montymul: on arm64 the assembly against the C code; elsewhere the C code against a slow reference
    if cc -O2 -Ibuild/bearssl-inc -Ithird_party/bearssl/inc -Ithird_party/bearssl/src -o "$OUT/$t" tests/crypto/$t.c $extra build/libbearssl.a > "$OUT/$t.build" 2>&1 \
       && "$OUT/$t" 20000 > "$OUT/$t.txt" 2>&1; then pass=$((pass+1)); else echo "FAIL crypto $t:"; cat "$OUT/$t.build" "$OUT/$t.txt" 2>/dev/null | head; fail=$((fail+1)); fi
done
# memory: loop and if conditions that make temporaries stay flat (C and QBE), peak RSS under 20 MB
for be in c qbe; do
    flag=""; [ $be = qbe ] && flag="--backend=qbe"
    $XISTO build $flag -o "$OUT/loop-temps-$be" tests/mem/loop-temps.ts > /dev/null 2>&1
    peak=$(python3 -c "
import resource, subprocess, sys
r = subprocess.run(['$OUT/loop-temps-$be'], capture_output=True, text=True)
m = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
print(r.stdout.strip(), m // 1048576 if sys.platform == 'darwin' else m // 1024)")
    set -- $peak
    if [ "$1" = "9000000" ] && [ "$2" -lt 20 ]; then pass=$((pass+1)); else echo "FAIL loop-temps ($be): output $1, peak $2 MB"; fail=$((fail+1)); fi
done
# TLS (BearSSL): HTTPS server + client, untrusted cert rejected, streaming, session resumption (port 8443)
# TLS end to end on each backend: AWS-LC (default when built, TLS 1.3) and BearSSL (TLS 1.2)
tls_ok() {
    grep -q '200 {"secure":true,"name":"isa"}' "$1" && grep -q "^200 200000" "$1" \
   && grep -q "fetch failed: certificate is not trusted" "$1" && grep -q "stream matches file" "$1" \
   && grep -q "python verified: TLSv1.2" "$1" && grep -q "session resumed: True" "$1" \
   && grep -q "slow reader: idle cpu" "$1" && grep -q "renegotiation refused" "$1" \
   && grep -q "close_notify sent" "$1" && grep -q "pem without newline + ecparam key: ok" "$1" \
   && grep -q "empty host error: fetch: invalid URL (empty host)" "$1" && grep -q "crlf value error: fetch: invalid value" "$1" \
   && grep -q "default protocol: $2" "$1"
}
XISTO_TLS=bearssl sh tests/tls/run.sh > "$OUT/tls.txt" 2>&1
if tls_ok "$OUT/tls.txt" TLSv1.2; then pass=$((pass+1)); else echo "FAIL tls (bearssl):"; cat "$OUT/tls.txt"; fail=$((fail+1)); fi
if [ -f build/awslc/lib/libssl.a ]; then
    XISTO_TLS=awslc sh tests/tls/run.sh > "$OUT/tls-awslc.txt" 2>&1
    if tls_ok "$OUT/tls-awslc.txt" TLSv1.3; then pass=$((pass+1)); else echo "FAIL tls (awslc):"; cat "$OUT/tls-awslc.txt"; fail=$((fail+1)); fi
fi
# stack traces: exact frames for runtime errors, throws and e.stack (both backends), HTTP 500 without details
if sh tests/stack/run.sh > "$OUT/stack.txt" 2>&1 && grep -q 'boom: 500 {"error":"internal error"}' "$OUT/stack.txt" \
   && grep -q 'leak: 500 {"error":"internal error"}' "$OUT/stack.txt" \
   && grep -q "item: 400 {'error': \"JSON: 'id' must be an integer\"}" "$OUT/stack.txt" \
   && grep -q "    at pick (tests/stack/server.ts:7:11)" "$OUT/stack.txt" && grep -q "    at boom (tests/stack/server.ts:10:31)" "$OUT/stack.txt" \
   && grep -q "    at late (tests/stack/server.ts:14:9)" "$OUT/stack.txt" && ! grep -q "warning" "$OUT/stack.txt"; then pass=$((pass+1)); else echo "FAIL stack:"; cat "$OUT/stack.txt"; fail=$((fail+1)); fi
# runtime functions that can fail or call back into user code must not be inlined (stack traces)
if python3 tools/noinline-scan.py --check runtime/xisto_rt.h runtime/xisto_net.h runtime/xisto_sqlite.h runtime/xisto_tls.h runtime/xisto_tls_awslc.h runtime/xisto_regex.h > "$OUT/noinline.txt" 2>&1; then pass=$((pass+1)); else echo "FAIL noinline:"; cat "$OUT/noinline.txt"; fail=$((fail+1)); fi
# TLS is linked only when the program can use https
if sh tests/tlsgate/run.sh > "$OUT/tlsgate.txt" 2>&1; then pass=$((pass+1)); else echo "FAIL tlsgate:"; cat "$OUT/tlsgate.txt"; fail=$((fail+1)); fi
fi

# pg: a pool asked for more queries at once than it has connections, against a live PostgreSQL when one is up
if sh tests/pg/run.sh > "$OUT/pg-pool.txt" 2>&1; then pass=$((pass+1)); else echo "FAIL pg pool:"; cat "$OUT/pg-pool.txt"; fail=$((fail+1)); fi
# typeorm: generated SQL for postgres and oracle; a live PostgreSQL when one is up (tests/typeorm/run.sh)
if sh tests/typeorm/run.sh > "$OUT/typeorm.txt" 2>&1; then pass=$((pass+1)); else echo "FAIL typeorm:"; cat "$OUT/typeorm.txt"; fail=$((fail+1)); fi
# class-validator: rule semantics against class-validator 0.14 (tests/validator/run.sh)
if sh tests/validator/run.sh > "$OUT/validator.txt" 2>&1; then pass=$((pass+1)); else echo "FAIL validator:"; cat "$OUT/validator.txt"; fail=$((fail+1)); fi
# packages: xisto add / install from a local git repository
if sh tests/pkg/run.sh > "$OUT/pkg.txt" 2>&1 && [ "$(grep -c '^ok' "$OUT/pkg.txt")" -ge 7 ] && ! grep -q FAIL "$OUT/pkg.txt"; then pass=$((pass+1)); else echo "FAIL packages:"; cat "$OUT/pkg.txt"; fail=$((fail+1)); fi
if sh examples/nest-features/test/run.sh > "$OUT/nest-features.txt" 2>&1 && grep -q '^ok: nest-features' "$OUT/nest-features.txt"; then pass=$((pass+1)); else echo "FAIL nest-features:"; cat "$OUT/nest-features.txt"; fail=$((fail+1)); fi
if sh examples/nest-auth/test/run.sh > "$OUT/nest-auth.txt" 2>&1 && grep -q '^ok: nest-auth' "$OUT/nest-auth.txt"; then pass=$((pass+1)); else echo "FAIL nest-auth:"; cat "$OUT/nest-auth.txt"; fail=$((fail+1)); fi

# redis package against a live server (skipped without one)
if sh tests/redis/run.sh > "$OUT/redis.txt" 2>&1; then pass=$((pass+1)); cat "$OUT/redis.txt"; else echo "FAIL redis:"; cat "$OUT/redis.txt"; fail=$((fail+1)); fi
# exceljs package: streaming XLSX (ZIP + deflate), checked as XML, by LibreOffice when installed, constant memory
if sh tests/exceljs/run.sh > "$OUT/exceljs.txt" 2>&1; then pass=$((pass+1)); cat "$OUT/exceljs.txt"; else echo "FAIL exceljs:"; cat "$OUT/exceljs.txt"; fail=$((fail+1)); fi
# @elastic/elasticsearch package against a fake cluster (Node)
if sh tests/elasticsearch/run.sh > "$OUT/elasticsearch.txt" 2>&1; then pass=$((pass+1)); cat "$OUT/elasticsearch.txt"; else echo "FAIL elasticsearch:"; cat "$OUT/elasticsearch.txt"; fail=$((fail+1)); fi

# oracledb package against a live database (skipped without XISTO_ORACLE_URL)
if sh tests/oracledb/run.sh > "$OUT/oracledb.txt" 2>&1; then pass=$((pass+1)); cat "$OUT/oracledb.txt"; else echo "FAIL oracledb:"; cat "$OUT/oracledb.txt"; fail=$((fail+1)); fi

# debugger: CLI transcripts and the agent driven over DAP (docs/debugger.md)
if sh tests/debug/run.sh > "$OUT/debug.txt" 2>&1; then pass=$((pass+1)); else echo "FAIL debugger:"; cat "$OUT/debug.txt"; fail=$((fail+1)); fi

# qbe: what it does not support is a compile error, never a crash (XB-013: calling a function value)
for f in tests/qbe/*.ts; do
    want=$(sed -n '1s|^// expect: ||p' "$f")
    $XISTO build --backend=qbe -o "$OUT/qbe-unsup" "$f" > "$OUT/qbe-unsup.txt" 2>&1; rc=$?
    if [ $rc -eq 1 ] && grep -qF -- "$want" "$OUT/qbe-unsup.txt"; then pass=$((pass+1)); else echo "FAIL qbe $f (exit $rc)"; head -3 "$OUT/qbe-unsup.txt"; fail=$((fail+1)); fi
done
# --dev (the runtime as a library, tools/rtsplit.py): a few programs must build and print what Node prints. Nothing
# else in the suite builds with --dev, and a runtime comment once broke every --dev build unnoticed
for n in arrays classes closures-ref nullish write-behind xisto-collections; do
    f=tests/ok/$n.ts
    [ -f "$f" ] && [ -f "$OUT/$n.node" ] || continue
    if $XISTO run --dev "$f" < /dev/null > "$OUT/$n.dev" 2>&1 && cmp -s "$OUT/$n.node" "$OUT/$n.dev"; then pass=$((pass+1)); else echo "FAIL --dev $f"; head -5 "$OUT/$n.dev"; fail=$((fail+1)); fi
done
# size budget: a program importing a 200-function module and using one must produce the same binary as the function alone
for backend in "" "--backend=qbe"; do
    $XISTO build $backend -o "$OUT/size-one" tests/size/uses-one.ts > /dev/null 2>&1 && $XISTO build $backend -o "$OUT/size-alone" tests/size/standalone.ts > /dev/null 2>&1 || { echo "FAIL size budget: build failed ($backend)"; fail=$((fail+1)); continue; }
    a=$(stat -f%z "$OUT/size-one"); b=$(stat -f%z "$OUT/size-alone")
    if [ "$("$OUT/size-one")" != "$("$OUT/size-alone")" ]; then echo "FAIL size budget: outputs differ"; fail=$((fail+1)); continue; fi
    if [ $((a - b)) -gt 512 ] || [ $((b - a)) -gt 512 ]; then
        echo "FAIL size budget ($backend): using 1 of 200 functions costs $a bytes vs $b standalone"; fail=$((fail+1))
    else
        pass=$((pass+1)); echo "size budget ok ($backend): $a vs $b bytes"
    fi
done
echo "tests: $pass passed, $fail failed ($qbe programs also verified through the qbe backend)"
[ "$fail" -eq 0 ]
