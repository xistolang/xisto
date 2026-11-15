// node:http with several servers in one program: an app and a metrics server at once on ports of their own (listen(0)),
// closing one keeps the other answering, then servers made one after another; 'connection', server.address() and
// req.socket (same output as Node)
import http from "node:http";

function portOf(s: http.Server): number {
  const a = s.address();
  return a !== null && typeof a === "object" ? a.port : 0;
}

function text(port: number, path: string, body: string | null = null): Promise<string> {
  return new Promise<string>((resolve) => {
    const req = http.request({ hostname: "127.0.0.1", port: port, path: path, method: body === null ? "GET" : "POST" }, (res) => {
      let t = "";
      res.on("data", (c) => (t += c));
      res.on("end", () => resolve(`${res.statusCode} ${t}`));
    });
    req.on("error", (e: any) => resolve(`error ${e.code}`));
    req.end(body);
  });
}

let hits = 0;
const app = http.createServer((req, res) => {
  hits++;
  let body = "";
  req.on("data", (c) => (body += c));
  req.on("end", () => res.end(`app ${req.method} ${req.url} ${body}`));
});
const metrics = http.createServer((req, res) => {
  res.setHeader("Content-Type", "text/plain");
  res.end(`hits ${hits}`);
});
let conns = 0;
app.on("connection", () => conns++);

function listen(s: http.Server): Promise<number> {
  return new Promise<number>((resolve) => s.listen(0, () => resolve(portOf(s))));
}
function close(s: http.Server): Promise<void> {
  return new Promise<void>((resolve) => s.close(() => resolve()));
}

async function main(): Promise<void> {
  const [a, m] = await Promise.all([listen(app), listen(metrics)]);
  console.log("two ports:", a > 0 && m > 0 && a !== m);
  console.log(await text(a, "/one", "olá"));
  console.log(await text(a, "/two"));
  console.log(await text(m, "/metrics"));
  const both = await Promise.all([text(a, "/x"), text(m, "/y"), text(a, "/z", "zz")]);
  console.log("at once:", both.join(" | "));
  console.log("socket:", (await new Promise<string>((resolve) => {
    const s = http.createServer((req, res) => {
      res.end("ok");
      resolve(`${req.socket.remoteAddress === "127.0.0.1" || req.socket.remoteAddress === "::ffff:127.0.0.1"} ${req.socket.localPort === portOf(s)}`);
      s.close();
    });
    s.listen(0, () => text(portOf(s), "/"));
  })));
  await close(metrics);
  console.log("metrics closed, listening:", metrics.listening, "address:", metrics.address());
  console.log("app still answers:", await text(a, "/after"));
  console.log("closed port refuses:", await text(m, "/gone"));
  for (let i = 0; i < 3; i++) {
    const s = http.createServer((req, res) => res.end(`server ${i}`));
    const p = await listen(s);
    console.log(await text(p, "/"));
    await close(s);
  }
  await close(app);
  console.log("app connections:", conns >= 1, "all closed");
}
main();
