// Node with the cluster module: N worker processes sharing the port (the Node way to use more cores).
import cluster from "node:cluster";
import { createServer } from "node:http";

const workers = Number(process.env.WORKERS ?? "1");
if (cluster.isPrimary && workers > 1) {
  for (let i = 0; i < workers; i++) cluster.fork();
  console.error(`node: cluster primary with ${workers} workers`);
} else {
  createServer((req, res) => {
    const url = new URL(req.url ?? "/", "http://localhost");
    if (url.pathname === "/ping") {
      res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
      res.end("pong");
    } else if (url.pathname === "/hello") {
      const name = url.searchParams.get("name") ?? "";
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end(`{"hello":"${name}","len":${name.length}}`);
    } else {
      res.writeHead(404);
      res.end("not found");
    }
  }).listen(8080, () => { if (workers <= 1) console.error("node: listening"); });
}
