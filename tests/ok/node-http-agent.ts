// http.Agent, http.globalAgent and the agent option: keep-alive reuse (req.reusedSocket, one connection), agent: false
// and an Agent without keepAlive ("Connection: close", a connection per request), maxSockets, getName, destroy
import * as http from "node:http";
import * as https from "node:https";
import { Agent } from "node:http";

let conns = 0;
const srv = http.createServer((req, res) => {
  setTimeout(() => res.end(`${req.headers["connection"]} ${req.url}`), 2);
});
srv.on("connection", () => conns++);

function get(port: number, path: string, agent: Agent | boolean | null): Promise<string> {
  return new Promise<string>((resolve) => {
    const o: http.RequestOptions = { host: "127.0.0.1", port: port, path: path };
    if (agent !== null) o.agent = agent;
    const req = http.get(o, (res) => {
      let b = "";
      res.setEncoding("utf8");
      res.on("data", (d: string) => {
        b += d;
      });
      res.on("end", () => resolve(`${b} reused=${req.reusedSocket}`));
    });
  });
}

srv.listen(0, "127.0.0.1", async () => {
  const addr = srv.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  const keep = new Agent({ keepAlive: true, maxSockets: 1 });
  const plain = new http.Agent();
  const runs: [string, Agent | boolean | null][] = [["default", null], ["false", false], ["plain", plain], ["keep", keep]];
  for (const [name, a] of runs) {
    const before = conns;
    const out: string[] = [];
    for (let i = 0; i < 3; i++) out.push(await get(port, "/" + name + i, a));
    console.log(name, out.join(" | "), "conns", conns - before);
  }
  // maxSockets 1: three requests at once go one after another on the one connection
  const before = conns;
  const all = await Promise.all([get(port, "/p0", keep), get(port, "/p1", keep), get(port, "/p2", keep)]);
  console.log(all.join(" | "), "conns", conns - before, keep.totalSocketCount);
  console.log(keep.keepAlive, keep.maxSockets, plain.maxSockets, plain.keepAlive, plain.keepAliveMsecs, plain.maxFreeSockets,
    plain.maxTotalSockets, plain.totalSocketCount, plain.defaultPort, plain.protocol, plain.scheduling);
  console.log(http.globalAgent.keepAlive, http.globalAgent instanceof Agent, https.globalAgent.defaultPort, https.globalAgent.protocol,
    new https.Agent({ keepAlive: true }).keepAlive, https.globalAgent instanceof http.Agent);
  console.log(keep.getName({ host: "a", port: 1 }), plain.getName({}), keep.getName({ host: "h", port: 2, localAddress: "x", family: 4 }));
  keep.destroy();
  console.log("after destroy", keep.totalSocketCount);
  // the server ends the idle connection: the next request opens a new one
  const k2 = new Agent({ keepAlive: true });
  console.log(await get(port, "/s0", k2));
  srv.closeIdleConnections();
  await new Promise<void>((r) => setTimeout(r, 20));
  const b2 = conns;
  console.log(await get(port, "/s1", k2), "conns", conns - b2);
  srv.close(() => console.log("closed"));
});
