// node:net: 'listening' comes on a tick before the event loop polls (so before a refused connection's error), one
// tick later when listen was given a host (Node looks the host up first); the same order as Node on every run
import net from "node:net";

const order: string[] = [];
const withHost = net.createServer();
withHost.listen(0, "127.0.0.1", () => order.push("listening with host"));
const anyHost = net.createServer();
anyHost.listen(0, "", () => order.push("listening"));
process.nextTick(() => order.push("tick"));
Promise.resolve().then(() => order.push("microtask"));
const refused = net.createConnection(1, "127.0.0.1");
refused.on("error", (err: net.SystemError) => {
  order.push(`error ${err.code}`);
  withHost.close();
  anyHost.close(() => console.log(order.join("\n")));
});
