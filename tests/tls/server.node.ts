import { createServer } from "node:https";
import { readFileSync } from "node:fs";
createServer({ cert: readFileSync("tests/tls/cert.pem"), key: readFileSync("tests/tls/key.pem") }, (req, res) => {
  const url = new URL(req.url ?? "/", "https://localhost");
  res.writeHead(200, { "Content-Type": "application/json" });
  res.end(`{"secure":true,"name":"${url.searchParams.get("name")}"}`);
}).listen(8443);
