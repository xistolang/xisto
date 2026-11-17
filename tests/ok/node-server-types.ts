// net.Server, http.Server and https.Server as type names (and net.Server as a class), next to each other
import * as net from "node:net";
import * as http from "node:http";
import * as https from "node:https";
import fs from "node:fs";

let tcp: net.Server | null = null;
let web: http.Server | null = null;
let secure: https.Server | null = null;

function describe(s: net.Server): string {
  return `net server listening=${s.listening}`;
}

const cert = fs.readFileSync("tests/tls/cert.pem", "utf8");
const key = fs.readFileSync("tests/tls/key.pem", "utf8");

tcp = new net.Server((sock: net.Socket) => {
  sock.end("bye\n");
});
web = http.createServer((req, res) => res.end("web"));
secure = https.createServer({ key: key, cert: cert }, (req, res) => res.end("secure"));
console.log(tcp instanceof net.Server, web instanceof http.Server, secure instanceof https.Server);
console.log(describe(tcp));
const t = tcp;
t.listen(0, "127.0.0.1", () => {
  console.log(describe(t));
  const addr = t.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  const c = net.connect(port, "127.0.0.1");
  c.setEncoding("utf8");
  c.on("data", (d: string) => process.stdout.write("got " + d));
  c.on("close", () => {
    t.close(() => console.log("closed"));
  });
});
