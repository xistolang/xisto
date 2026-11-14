// node:https and node:tls: an https server with a certificate, a tls.connect that fails without its CA, then
// https.get and tls.connect that trust it through the ca option (same output as Node)
import https from "node:https";
import tls from "node:tls";
import fs from "node:fs";

const cert = fs.readFileSync("tests/tls/cert.pem", "utf8");
const key = fs.readFileSync("tests/tls/key.pem", "utf8");

const server = https.createServer({ key: key, cert: cert }, (req, res) => {
  res.setHeader("Content-Type", "text/plain");
  res.end(`secure ${req.method} ${req.url}`);
});

function viaTls(port: number): void {
  const s = tls.connect({ host: "localhost", port: port, ca: cert, servername: "localhost" }, () => {
    console.log("tls: secureConnect", s.authorized);
    s.write("GET /raw HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
  });
  let got = "";
  s.setEncoding("utf8");
  s.on("data", (d: string) => {
    got += d;
  });
  s.on("end", () => {
    const head = got.split("\r\n")[0];
    const body = got.slice(got.indexOf("\r\n\r\n") + 4);
    console.log("tls:", head, JSON.stringify(body));
    server.close(() => console.log("closed"));
  });
}

server.listen(0, () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  const untrusted = tls.connect({ host: "localhost", port: port, servername: "localhost" });
  untrusted.on("error", (err: Error) => {
    console.log("untrusted: error", err.message.length > 0);
    https.get({ hostname: "localhost", port: port, path: "/hello?x=1", ca: cert }, (res) => {
      let body = "";
      res.setEncoding("utf8");
      res.on("data", (c: string) => {
        body += c;
      });
      res.on("end", () => {
        console.log("https.get:", res.statusCode, body);
        viaTls(port);
      });
    });
  });
});
