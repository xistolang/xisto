// node:net: an echo server and a client in one program; isIP (same output as Node)
import net from "node:net";

console.log(net.isIP("127.0.0.1"), net.isIP("::1"), net.isIP("2001:db8::ff00:42:8329"), net.isIP("300.1.1.1"), net.isIP("x"));
console.log(net.isIPv4("10.0.0.1"), net.isIPv6("10.0.0.1"), net.isIPv6("fe80::1%lo0"));

const server = net.createServer((socket) => {
  socket.setEncoding("utf8");
  socket.on("data", (d: string) => {
    socket.write(`echo:${d}`);
  });
  socket.on("end", () => {
    socket.end("bye");
  });
});
server.listen(0, "127.0.0.1", () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  console.log("listening", port > 0);
  const client = net.createConnection(port, "127.0.0.1", () => {
    console.log("connected", client.remotePort === port);
    client.write("hi");
  });
  let got = "";
  client.setEncoding("utf8");
  client.on("data", (d: string) => {
    got += d;
    if (got === "echo:hi") client.end();
  });
  client.on("end", () => console.log("client end", got));
  client.on("close", (hadError: boolean) => {
    console.log("client close", hadError);
    server.close(() => console.log("server closed"));
  });
});

const refused = net.createConnection(1, "127.0.0.1");
refused.on("error", (err: net.SystemError) => console.log("error", err.code));
