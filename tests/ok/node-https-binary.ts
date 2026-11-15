// node:https with binary bodies and streaming: bytes 0..255 both ways over TLS, a chunked response written in parts
// (same output as Node)
import https from "node:https";
import fs from "node:fs";

const cert = fs.readFileSync("tests/tls/cert.pem", "utf8");
const key = fs.readFileSync("tests/tls/key.pem", "utf8");
const all = new Uint8Array(256);
for (let i = 0; i < 256; i++) all[i] = 255 - i;

const server = https.createServer({ key: key, cert: cert }, (req, res) => {
  const parts: Buffer[] = [];
  req.on("data", (c: Buffer) => parts.push(c));
  req.on("end", () => {
    const body = Buffer.concat(parts);
    if (req.url === "/echo") {
      res.end(body);
      return;
    }
    res.write(Buffer.from(all.subarray(0, 128)));
    setTimeout(() => res.end(Buffer.from(all.subarray(128))), 30);
  });
});

function call(port: number, path: string, body: Uint8Array | null): Promise<string> {
  return new Promise<string>((resolve) => {
    const req = https.request({ hostname: "localhost", port: port, path: path, method: body === null ? "GET" : "POST", ca: cert }, (res) => {
      const parts: Buffer[] = [];
      res.on("data", (c: Buffer) => parts.push(c));
      res.on("end", () => {
        const b = Buffer.concat(parts);
        let same = b.length === 256;
        for (let i = 0; i < b.length && same; i++) if (b[i] !== 255 - i) same = false;
        resolve(`${path} ${res.statusCode} ${res.headers["transfer-encoding"] ?? res.headers["content-length"]} ${b.length} exact: ${same}`);
      });
    });
    req.on("error", (e) => resolve(`error ${e.message}`));
    req.end(body);
  });
}

server.listen(0, async () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  console.log(await call(port, "/echo", all));
  console.log(await call(port, "/parts", null));
  server.close(() => console.log("closed"));
});
