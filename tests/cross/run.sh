#!/bin/sh
# xisto build --target: every tests/ok program built on this machine for Linux arm64 and x86_64 (static, musl,
# through zig) and run in the Linux test containers (tests/linux/Dockerfile), against Node's outputs from the
# main suite (build/tests/*.node: run `make test` first). Needs zig and Docker.
cd "$(dirname "$0")/../.." || exit 1
command -v zig > /dev/null || { echo "skip: zig not installed (brew install zig)"; exit 0; }
docker info > /dev/null 2>&1 || { echo "skip: Docker not running"; exit 0; }
ls build/tests/*.node > /dev/null 2>&1 || { echo "run make test first (Node's outputs)"; exit 1; }
W=bench/out/cross; rm -rf "$W"; mkdir -p "$W/oracle" "$W/ok"
cp build/tests/*.node "$W/oracle/"; cp -r tests/ok/. "$W/ok/"
export XISTO_NODE_COMPAT=1
fail=0
for t in linux-arm64 linux-x64; do
    mkdir -p "$W/bin/$t"
    for f in tests/ok/*.ts; do
        n=$(basename "$f" .ts)
        [ -f "$W/oracle/$n.node" ] || continue
        build/xisto build --target=$t "$f" -o "$W/bin/$t/$n" > "$W/bin/$t/$n.build" 2>&1 || { echo "FAIL build $t $n"; fail=$((fail+1)); }
    done
done
cat > "$W/run.sh" <<'IN'
cd /tmp && cp -r /x/ok /tmp/ok && cd /tmp/ok
pass=0; fail=0
for b in /x/bin/$1/*; do
  case "$b" in *.build) continue;; esac
  n=$(basename "$b")
  TZ=America/New_York "$b" > "/tmp/$n.out" 2>&1 < /dev/null
  if cmp -s "/tmp/$n.out" "/x/oracle/$n.node"; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL $1 $n"; diff "/x/oracle/$n.node" "/tmp/$n.out" | head -4; fi
done
echo "$1: $pass passed, $fail failed"
[ $fail -eq 0 ]
IN
for p in linux-arm64:arm64 linux-x64:amd64; do
    t=${p%%:*}; a=${p##*:}
    docker build -q --platform linux/$a -t "xisto-test:linux-$a" tests/linux > /dev/null || exit 1
    docker run --rm --platform linux/$a -v "$PWD/$W":/x:ro "xisto-test:linux-$a" sh /x/run.sh $t || fail=$((fail+1))
done
# TLS: the HTTPS server and client of tests/tls, built for each target (BearSSL compiled for it), talking in the container
mkdir -p "$W/tls/tests/tls"; cp tests/tls/*.pem "$W/tls/tests/tls/"
for p in linux-arm64:arm64 linux-x64:amd64; do
    t=${p%%:*}; a=${p##*:}
    build/xisto build --target=$t tests/tls/server.ts -o "$W/tls/server-$t" > /dev/null || { echo "FAIL build tls server $t"; fail=$((fail+1)); continue; }
    build/xisto build --target=$t tests/tls/client.ts -o "$W/tls/client-$t" > /dev/null || { echo "FAIL build tls client $t"; fail=$((fail+1)); continue; }
    out=$(docker run --rm --platform linux/$a -v "$PWD/$W/tls":/x:ro "xisto-test:linux-$a" sh -c "cd /x && (./server-$t > /dev/null 2>&1 &); sleep 1; NODE_EXTRA_CA_CERTS=tests/tls/cert.pem ./client-$t 2>&1 | head -1; curl -sk https://127.0.0.1:8443/stream | cmp -s - tests/tls/cert.pem && echo stream-ok")
    if echo "$out" | grep -q '"secure":true' && echo "$out" | grep -q stream-ok; then echo "$t tls: ok"; else echo "FAIL $t tls: $out"; fail=$((fail+1)); fi
done
# Windows (x64 and arm64): built here, every tests/ok program plus the HTTP and HTTPS pairs; they run on a real
# Windows machine (tests/windows/run.sh), so here a failed build is what this catches
for t in windows-x64 windows-arm64; do
    mkdir -p "$W/bin/$t"
    nb=0
    for f in tests/ok/*.ts tests/http/server.ts tests/http/client.ts tests/tls/server.ts tests/tls/client.ts; do
        n=$(echo "$f" | tr / _)
        if build/xisto build --target=$t "$f" -o "$W/bin/$t/$n.exe" > "$W/bin/$t/$n.build" 2>&1; then nb=$((nb+1)); else echo "FAIL build $t $f"; fail=$((fail+1)); fi
    done
    echo "$t: $nb built"
done
[ $fail -eq 0 ]
