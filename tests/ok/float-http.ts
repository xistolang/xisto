// an HTTP handler that fires a slow task and answers at once: the response does not wait for the task
import http from "node:http";

const log: string[] = [];

function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve) => setTimeout(resolve, ms));
}

async function slowTask(id: string): Promise<void> {
  log.push("task start " + id);
  await sleep(40);
  log.push("task end " + id);
}

const server = http.createServer((req, res) => {
  void slowTask(req.url ?? "/");
  log.push("responding");
  res.end("ok");
});

server.listen(0, "127.0.0.1", async () => {
  const addr = server.address();
  const port = addr !== null && typeof addr === "object" ? addr.port : 0;
  const t0 = Date.now();
  const r = await fetch(`http://127.0.0.1:${port}/job`);
  const body = await r.text();
  log.push("got " + body);
  const fast = Date.now() - t0 < 35;
  await sleep(60);
  server.close();
  for (const l of log) console.log(l);
  console.log("answered before the task ended:", fast);
});
