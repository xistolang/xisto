// (node-https-backpressure-bearssl.ts with the default TLS backend)
// node:https with a full socket: the handler writes n chunks of 8000 bytes, then ends with one more, all at once
// (the client cannot read meanwhile), for n from 0 up, so for one n the socket fills exactly at the last write.
// Every response must arrive whole, over a connection the server closes (Connection: close) and over a
// keep-alive one (which must not wait for the client to write before the last bytes go out).
import https from "node:https";
import fs from "node:fs";

const cert = fs.readFileSync("tests/tls/cert.pem", "utf8");
const key = fs.readFileSync("tests/tls/key.pem", "utf8");
const CHUNK = 8000;
const chunk = Buffer.alloc(CHUNK, 120);
const N = 160;

const server = https.createServer({ key: key, cert: cert }, (req, res) => {
  const n = Number((req.url ?? "/0").slice(1));
  res.setHeader("Content-Length", String((n + 1) * CHUNK));
  for (let i = 0; i < n; i++) res.write(chunk);
  res.end(chunk);
});

function get(port: number, n: number, close: boolean): Promise<number> {
  return new Promise<number>((resolve) => {
    let got = 0;
    let done = false;
    const finish = (v: number): void => {
      if (done) return;
      done = true;
      resolve(v);
    };
    const req = https.request({ hostname: "localhost", port: port, path: `/${n}`, ca: cert, headers: { Connection: close ? "close" : "keep-alive" } }, (res) => {
      res.on("data", (c: Buffer) => {
        got += c.length;
      });
      res.on("end", () => finish(got));
      res.on("aborted", () => finish(got));
      res.on("error", (_e: Error) => finish(got));
    });
    req.setTimeout(3000, () => {
      finish(got);
      req.destroy();
    });
    req.on("error", (_e: Error) => finish(got));
    req.end();
  });
}

server.listen(0, async () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  for (const close of [true, false]) {
    let short = 0;
    for (let n = 0; n < N; n++) {
      const got = await get(port, n, close);
      if (got !== (n + 1) * CHUNK) {
        short++;
        if (short <= 3) console.log(`short: n=${n} got ${got} of ${(n + 1) * CHUNK}`);
      }
    }
    console.log(`${close ? "close" : "keep-alive"}: ${N} responses, ${short} short`);
  }
  server.close(() => console.log("closed"));
});
