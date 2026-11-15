// node:http with binary bodies: a response of bytes 0..255 (and gzip from zlib) arrives byte for byte with its
// Content-Length in bytes; a request body of bytes 0..255 reaches the server as Buffers; text after setEncoding;
// body += chunk without setEncoding gives the UTF-8 text (same output as Node)
import http from "node:http";
import zlib from "node:zlib";

const all = new Uint8Array(256);
for (let i = 0; i < 256; i++) all[i] = i;

function sum(b: Uint8Array): number {
  let s = 0;
  for (let i = 0; i < b.length; i++) s = (s * 31 + b[i]) % 1000003;
  return s;
}

const server = http.createServer((req, res) => {
  if (req.url === "/bytes") {
    res.setHeader("Content-Type", "application/octet-stream");
    res.end(Buffer.from(all));
  } else if (req.url === "/u8") {
    res.write(all.subarray(0, 100));
    res.end(all.subarray(100));
  } else if (req.url === "/gzip") {
    res.writeHead(200, { "Content-Type": "text/plain", "Content-Encoding": "gzip" });
    res.end(zlib.gzipSync(Buffer.from("héllo wörld ".repeat(50))));
  } else if (req.url === "/upload") {
    const chunks: Buffer[] = [];
    let kinds = "";
    req.on("data", (c: Buffer) => {
      if (kinds.length === 0) kinds = Buffer.isBuffer(c) ? "Buffer" : "other";
      chunks.push(c);
    });
    req.on("end", () => {
      const body = Buffer.concat(chunks);
      res.end(`got ${body.length} bytes, ${kinds}, sum ${sum(body)}, first ${body[0]} last ${body[body.length - 1]}`);
    });
  } else if (req.url === "/text") {
    let body = "";
    req.on("data", (c) => (body += c));
    req.on("end", () => res.end(`text: ${body} (${body.length})`));
  } else {
    let body = "";
    req.setEncoding("utf8");
    req.on("data", (c: string) => (body += c));
    req.on("end", () => res.end(`utf8: ${typeof body} ${body}`));
  }
});

function get(port: number, path: string): Promise<Buffer> {
  return new Promise<Buffer>((resolve) => {
    http.get({ hostname: "127.0.0.1", port: port, path: path }, (res) => {
      const parts: Buffer[] = [];
      res.on("data", (c: Buffer) => parts.push(c));
      res.on("end", () => {
        const b = Buffer.concat(parts);
        console.log(path, res.statusCode, "content-length:", res.headers["content-length"] ?? "-", "transfer-encoding:", res.headers["transfer-encoding"] ?? "-", "received:", b.length);
        resolve(b);
      });
    });
  });
}

function post(port: number, path: string, body: Uint8Array | string): Promise<string> {
  return new Promise<string>((resolve) => {
    const req = http.request({ hostname: "127.0.0.1", port: port, path: path, method: "POST" }, (res) => {
      let text = "";
      res.setEncoding("utf8");
      res.on("data", (c: string) => (text += c));
      res.on("end", () => resolve(text));
    });
    req.end(body);
  });
}

server.listen(0, async () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  const b = await get(port, "/bytes");
  console.log("bytes exact:", sum(b) === sum(all), b[0], b[127], b[128], b[255]);
  const u = await get(port, "/u8");
  console.log("u8 exact:", sum(u) === sum(all));
  const g = await get(port, "/gzip");
  console.log("gunzip:", zlib.gunzipSync(g).toString().slice(0, 24), zlib.gunzipSync(g).length);

  const r = await fetch(`http://127.0.0.1:${port}/bytes`);
  const fb = new Uint8Array(await r.arrayBuffer());
  console.log("fetch arrayBuffer:", fb.length, sum(fb) === sum(all), r.headers.get("content-length"));

  console.log(await post(port, "/upload", all));
  const big = new Uint8Array(300000);
  for (let i = 0; i < big.length; i++) big[i] = (i * 7) % 256;
  console.log(await post(port, "/upload", big));
  console.log(await post(port, "/text", "olá, mundo"));
  console.log(await post(port, "/utf8", "çã"));
  const rf = await fetch(`http://127.0.0.1:${port}/upload`, { method: "POST", body: Buffer.from(all) });
  console.log("fetch upload:", await rf.text());
  server.close(() => console.log("closed"));
});
