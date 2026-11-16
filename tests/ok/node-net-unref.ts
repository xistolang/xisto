// An unref'd socket (client or server side) and an unref'd server do not keep the program alive: the peer never
// answers, and the program still ends once its referenced work (a timer) is done (XB-226).
import * as net from "node:net";
import * as http from "node:http";

let connections = 0;
const server = net.createServer((s: net.Socket) => {
  s.unref(); // (the server side never writes: each client waits forever)
  connections++;
  if (connections === 2) {
    server.unref(); // (referenced until both clients are in: the program cannot end before)
    setTimeout(() => console.log(`timer: the server saw ${connections} connections`), 20);
  }
});
server.listen(0, "127.0.0.1", () => {
  const port = (server.address() as net.AddressInfo).port;

  const c = net.createConnection(port, "127.0.0.1", () => {
    console.log("client: connected");
    c.write("hello");
    const req = http.get(`http://127.0.0.1:${port}/`, (_res: http.IncomingMessage) => console.log("http: response (never)"));
    req.on("socket", (s: net.Socket) => {
      s.unref();
      console.log("http: socket");
    });
    req.on("error", (_e: Error) => console.log("http: error (never)"));
  });
  c.unref();
  c.ref();
  c.unref();
  c.on("data", (_d: Buffer) => console.log("client: data (never)"));
  c.on("close", () => console.log("client: close (never)"));
});
process.on("exit", (code: number) => console.log(`exit ${code}`));
