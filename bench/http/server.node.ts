// REST server on Node (node:http). Same routes as server.xisto.ts.
import { createServer } from "node:http";

const server = createServer((req, res) => {
  const url = new URL(req.url ?? "/", "http://localhost");
  if (req.method === "GET" && url.pathname === "/ping") {
    res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
    res.end("pong");
  } else if (req.method === "GET" && url.pathname === "/hello") {
    const name = url.searchParams.get("name") ?? "";
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(`{"hello":"${name}","len":${name.length}}`);
  } else if (req.method === "POST" && url.pathname === "/echo") {
    const chunks: Buffer[] = [];
    req.on("data", (c: Buffer) => chunks.push(c));
    req.on("end", () => {
      res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
      res.end(Buffer.concat(chunks));
    });
  } else {
    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("not found");
  }
});
server.listen(8080, () => console.error("node: listening on http://127.0.0.1:8080"));
