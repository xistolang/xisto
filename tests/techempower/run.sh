#!/bin/sh
# TechEmpower phase 1: the xisto native app (bench/techempower/xisto/native) answers /plaintext and /json as the
# TFB validator requires (bench/techempower/validate.py: headers, body, a fresh Date, pipelining), with one event
# loop and with 2 workers. The port is a free one picked by the OS (no fixed port: other suites run in parallel).
cd "$(dirname "$0")/../.." || exit 1
X=./build/xisto; T=${TMPDIR:-/tmp}/xisto-techempower-test; mkdir -p "$T"
$X build -o "$T/native" bench/techempower/xisto/native/main.ts || exit 1
for w in 1 2; do
    PORT=$(python3 -c "import socket; s=socket.socket(); s.bind(('127.0.0.1',0)); print(s.getsockname()[1])")
    PORT=$PORT XISTO_WORKERS=$w XISTO_LOCK=0 "$T/native" > "$T/server-$w.log" 2>&1 & SRV=$!
    python3 -c "
import socket,time
t0=time.time()
while time.time()-t0<10:
    try: socket.create_connection(('127.0.0.1',$PORT),timeout=0.2).close(); break
    except OSError: time.sleep(0.01)"
    echo "workers $w:"
    python3 bench/techempower/validate.py "http://127.0.0.1:$PORT"
    kill $SRV; wait $SRV 2>/dev/null
done
