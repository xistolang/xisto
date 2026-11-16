// node:http's server and malformed request framing, answered as Node's parser does: Content-Length together with
// Transfer-Encoding: chunked is a 400 (request smuggling), and a chunk size that is not all hex digits ("1g") is a
// 400 too; a good chunked request after them still works. Also fs.readdirSync's order on many entries.
import http from "node:http";
import net from "node:net";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

type int = number;

const server = http.createServer((req, res) => {
  let body = "";
  req.setEncoding("utf8");
  req.on("data", (c: string) => {
    body += c;
  });
  req.on("end", () => res.end(`got ${body.length}: ${body}`));
});

function raw(port: int, text: string): Promise<string> {
  return new Promise<string>((resolve) => {
    let got = "";
    const s = net.createConnection(port, "127.0.0.1", () => s.write(text));
    s.setEncoding("utf8");
    s.on("data", (d: string) => {
      got += d;
    });
    s.on("close", () => resolve(got.split("\r\n")[0] + " | " + got.slice(got.indexOf("\r\n\r\n") + 4)));
  });
}

server.listen(0, "127.0.0.1", async () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  console.log(await raw(port, "POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\nhello\r\n0\r\n\r\n"));
  console.log(await raw(port, "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n1g\r\nhello\r\n0\r\n\r\n"));
  console.log(await raw(port, "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\nhello\r\nA\r\n0123456789\r\n0\r\n\r\n"));
  server.close();

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "xisto-readdir-"));
  const names: string[] = [];
  for (let i = 0; i < 300; i++) names.push(`f${(i * 7919) % 1000}.txt`);
  for (const n of names) fs.writeFileSync(path.join(dir, n), "");
  fs.mkdirSync(path.join(dir, "B"));
  fs.mkdirSync(path.join(dir, "a"));
  const got = fs.readdirSync(dir);
  console.log(got.length, got.slice(0, 5).join(","), got.slice(-3).join(","));
  fs.rmSync(dir, { recursive: true, force: true });
});
