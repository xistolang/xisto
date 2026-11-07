#!/bin/sh
# TLS end to end: HTTPS server (EC cert from tests/tls) and HTTPS client, trust rules, streaming, resumption.
export XISTO_NODE_COMPAT=1 # these programs also run on Node for comparison: node: imports allowed
cd "$(dirname "$0")/../.." || exit 1
X=./build/xisto; T=${TMPDIR:-/tmp}/xisto-tls-test; mkdir -p $T
$X build -o $T/server tests/tls/server.ts || exit 1
$X build -o $T/client tests/tls/client.ts || exit 1
$T/server > $T/server.log 2>&1 & SRV=$!
python3 -c "
import socket,time
t0=time.time()
while time.time()-t0<10:
    try: socket.create_connection(('127.0.0.1',8443),timeout=0.2).close(); break
    except OSError: time.sleep(0.01)"
echo "-- trusted (NODE_EXTRA_CA_CERTS):"
NODE_EXTRA_CA_CERTS=tests/tls/cert.pem $T/client 2>&1
echo "-- untrusted:"
$T/client 2>&1 | head -2
echo "-- stream:"
curl -sk https://127.0.0.1:8443/stream | cmp -s - tests/tls/cert.pem && echo "stream matches file" || echo "stream DIFFERS"
python3 - <<'PY'
import socket, ssl
ctx = ssl.create_default_context(cafile="tests/tls/cert.pem"); ctx.maximum_version = ssl.TLSVersion.TLSv1_2
def one(sess):
    s = ctx.wrap_socket(socket.create_connection(("127.0.0.1", 8443)), session=sess, server_hostname="localhost")
    ver = s.version()
    s.sendall(b"GET /hello?name=py HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
    data = b""
    while True:
        d = s.recv(65536)
        if not d: break
        data += d
    out = (s.session, s.session_reused, ver, data.split(b"\r\n\r\n", 1)[-1].decode()); s.close(); return out
sess, _, ver, body = one(None)
print("python verified:", ver, body)
_, reused, _, _ = one(sess)
print("session resumed:", reused)
d = ssl.create_default_context(cafile="tests/tls/cert.pem")
s = d.wrap_socket(socket.create_connection(("127.0.0.1", 8443)), server_hostname="localhost")
print("default protocol:", s.version()); s.close()
PY
# a client that stops reading a big response but keeps sending: the server must not spin (it did, at 100% CPU)
python3 - $SRV <<'PY'
import socket, ssl, time, subprocess, sys
def cpu(pid):
    t = subprocess.run(["ps", "-o", "time=", "-p", pid], capture_output=True, text=True).stdout.strip()
    parts = [float(x) for x in t.replace("-", ":").split(":")]
    return sum(v * 60 ** i for i, v in enumerate(reversed(parts)))
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
s = ctx.wrap_socket(socket.create_connection(("127.0.0.1", 8443)))
s.sendall(b"GET /huge HTTP/1.1\r\nHost: x\r\n\r\n")
time.sleep(1)
c0 = cpu(sys.argv[1])
for _ in range(6):
    s.sendall(b"GET /hello HTTP/1.1\r\nHost: x\r\n\r\n"); time.sleep(0.25)
used = cpu(sys.argv[1]) - c0
print("slow reader:", "idle cpu" if used < 0.5 else "SPINNING", "(%.2f s of CPU in 1.5 s)" % used)
s.close()
PY
# renegotiation is refused; close_notify is sent before closing
(printf 'R\n'; sleep 1) | perl -e 'alarm 5; exec @ARGV' openssl s_client -connect 127.0.0.1:8443 -tls1_2 2>&1 | grep -q "no renegotiation" && echo "renegotiation refused" || echo "renegotiation ACCEPTED"
(printf 'GET /hello HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n'; sleep 1) | perl -e 'alarm 5; exec @ARGV' openssl s_client -connect 127.0.0.1:8443 -tls1_2 -ign_eof > $T/cn.txt 2>&1
grep -q "unexpected eof" $T/cn.txt && echo "close_notify MISSING" || echo "close_notify sent"
kill $SRV; wait $SRV 2>/dev/null
# a PEM certificate without the final newline and an `openssl ecparam -genkey` key (EC PARAMETERS block first)
openssl ecparam -name prime256v1 -genkey -out $T/k.pem 2>/dev/null
openssl req -new -x509 -key $T/k.pem -out $T/c.pem -days 2 -subj /CN=localhost -addext "subjectAltName=DNS:localhost" 2>/dev/null
printf %s "$(cat $T/c.pem)" > $T/c_nonl.pem
cat > $T/s2.ts <<EOF2
function hello(req: HttpRequest): HttpResponse {
  return HttpResponse.text("hi");
}
const app: Server = new Server();
app.get("/hello", hello);
app.listen(18445, { cert: "$T/c_nonl.pem", key: "$T/k.pem" });
EOF2
cat > $T/c2.ts <<'EOF2'
async function tryit(label: string, url: string, value: string): Promise<void> {
  try {
    const r: Response = await fetch(url, { headers: { "x-a": value } });
    console.log(label, r.status, await r.text());
  } catch (e) {
    console.log(label, "error:", e.message);
  }
}
await tryit("pem without newline + ecparam key:", "https://localhost:18445/hello", "1");
await tryit("empty host", "https://:18445/hello", "1");
await tryit("crlf value", "https://localhost:18445/hello", "1\r\nX-Evil: 1");
await tryit("crlf path", "https://localhost:18445/a\r\nb", "1");
EOF2
$X build -o $T/s2 $T/s2.ts > /dev/null || exit 1
$X build -o $T/c2 $T/c2.ts > /dev/null 2>&1 || exit 1
$T/s2 > $T/s2.log 2>&1 & S2=$!
python3 -c "
import socket,time
t0=time.time()
while time.time()-t0<10:
    try: socket.create_connection(('127.0.0.1',18445),timeout=0.2).close(); break
    except OSError: time.sleep(0.01)"
NODE_EXTRA_CA_CERTS=$T/c.pem $T/c2 | sed 's/^pem without newline + ecparam key: 200 hi$/pem without newline + ecparam key: ok/'
kill $S2; wait $S2 2>/dev/null
