// server.listen in Node's forms: listen(cb), listen(port, cb), listen(port, host, cb), listen(port, host, backlog, cb)
import net from "node:net";
import http from "node:http";

function echo(label: string, port: number, done: () => void): void {
  const c = net.connect(port, "127.0.0.1", () => c.write(label));
  c.on("data", (d) => {
    console.log("net", String(d));
    c.end();
    done();
  });
}
function get(label: string, port: number, done: () => void): void {
  http.get(`http://127.0.0.1:${port}/`, (res) => {
    let b = "";
    res.on("data", (d) => (b += String(d)));
    res.on("end", () => {
      console.log("http", label, b);
      done();
    });
  });
}
const onConn = (c: net.Socket) => {
  c.on("data", (d) => c.write(d));
};
const onReq = (req: http.IncomingMessage, res: http.ServerResponse) => {
  res.end("ok");
};

const a = net.createServer(onConn);
a.listen(0, () => echo("port-cb", (a.address() as any).port, () => a.close(() => {
  const b = net.createServer(onConn);
  b.listen(0, "127.0.0.1", () => echo("host-cb", (b.address() as any).port, () => b.close(() => {
    const c = net.createServer(onConn);
    c.listen(0, "127.0.0.1", 511, () => echo("backlog-cb", (c.address() as any).port, () => c.close(() => {
      const d = net.createServer(onConn);
      d.listen(() => echo("cb-only", (d.address() as any).port, () => d.close(() => {
        const h1 = http.createServer(onReq);
        h1.listen(0, () => get("port-cb", (h1.address() as any).port, () => h1.close(() => {
          const h2 = http.createServer(onReq);
          h2.listen(0, "127.0.0.1", 511, () => get("backlog-cb", (h2.address() as any).port, () => h2.close(() => {
            const h3 = http.createServer(onReq);
            h3.listen(() => get("cb-only", (h3.address() as any).port, () => h3.close(() => console.log("done"))));
          })));
        })));
      })));
    })));
  })));
})));
