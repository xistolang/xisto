// node:diagnostics_channel: the channels node:http and fetch publish to (as an APM agent subscribes to them): a
// subscriber sees each request and its status, and adds a header that the server then receives
import dc from "node:diagnostics_channel";
import http from "node:http";

dc.subscribe("http.server.request.start", (msg: any) => {
  console.log("server start:", msg.request.method, msg.request.url, "trace:", msg.request.headers["x-trace"] ?? "-");
});
dc.subscribe("http.server.response.finish", (msg: any) => {
  console.log("server finish:", msg.request.url, msg.response.statusCode);
});
dc.subscribe("undici:request:create", (msg: any) => {
  console.log("fetch create:", msg.request.method, msg.request.path);
  msg.request.addHeader("x-trace", "from-fetch");
});
dc.subscribe("undici:request:headers", (msg: any) => {
  console.log("fetch headers:", msg.request.path, msg.response.statusCode);
});
dc.subscribe("http.client.request.start", (msg: any) => {
  console.log("client start:", msg.request.method, msg.request.path);
});
dc.subscribe("http.client.response.finish", (msg: any) => {
  console.log("client finish:", msg.request.path, msg.response.statusCode);
});
const ch = dc.channel("custom");
console.log("custom has subscribers:", ch.hasSubscribers);
const onCustom = (msg: any, name: string): void => console.log("custom got", msg.n, name);
ch.subscribe(onCustom);
ch.publish({ n: 1 });
ch.unsubscribe(onCustom);
ch.publish({ n: 2 });
console.log("custom has subscribers:", ch.hasSubscribers, dc.hasSubscribers("custom"));

const server = http.createServer((req, res) => {
  res.statusCode = req.url === "/missing" ? 404 : 200;
  res.end("ok");
});
server.listen(0, async () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  const r = await fetch(`http://127.0.0.1:${port}/a?x=1`);
  await r.text();
  const r2 = await fetch(`http://127.0.0.1:${port}/missing`);
  await r2.text();
  http.get({ hostname: "127.0.0.1", port: port, path: "/via-http" }, (res) => {
    res.resume();
    res.on("end", () => server.close());
  });
});
