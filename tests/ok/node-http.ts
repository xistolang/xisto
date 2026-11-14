// node:http: createServer with IncomingMessage/ServerResponse, listen(0) and address(), close(); the client side
// with fetch and http.get (same output as Node)
import http from "node:http";

const server = http.createServer((req, res) => {
  let body = "";
  req.setEncoding("utf8");
  req.on("data", (chunk: string) => {
    body += chunk;
  });
  req.on("end", () => {
    if (req.url === "/json") {
      res.setHeader("Content-Type", "application/json");
      res.end(JSON.stringify({ method: req.method, body: body, type: req.headers["content-type"] ?? "-" }));
      return;
    }
    if (req.url === "/status") {
      res.writeHead(418, { "X-Tea": "yes" });
      res.end("teapot");
      return;
    }
    if (req.url === "/later") {
      setTimeout(() => {
        res.end("answered later");
      }, 20);
      return;
    }
    res.statusCode = 201;
    res.setHeader("X-Custom", "abc");
    console.log("has x-custom:", res.hasHeader("x-custom"), res.getHeader("X-CUSTOM"));
    res.write("hello ");
    res.end(`${req.method} ${req.url} ${req.headers["x-who"] ?? "-"}`);
  });
});

server.on("listening", () => console.log("listening event"));
server.listen(0, async () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  console.log("port > 0:", port > 0);
  const base = `http://127.0.0.1:${port}`;

  const r1 = await fetch(`${base}/a/b?x=1&y=2`, { headers: { "X-Who": "ana" } });
  console.log(r1.status, r1.headers.get("x-custom"), await r1.text());

  const r2 = await fetch(`${base}/json`, { method: "POST", headers: { "Content-Type": "text/plain" }, body: "payload" });
  console.log(r2.status, r2.headers.get("content-type"), await r2.text());

  const r3 = await fetch(`${base}/later`);
  console.log(r3.status, await r3.text());

  http.get(`${base}/status`, (res) => {
    let data = "";
    res.setEncoding("utf8");
    res.on("data", (chunk: string) => {
      data += chunk;
    });
    res.on("end", () => {
      console.log(res.statusCode, res.statusMessage, res.headers["x-tea"], data);
      server.close(() => console.log("closed"));
    });
  });
});
console.log("listen returned at once");
