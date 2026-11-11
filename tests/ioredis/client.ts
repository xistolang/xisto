// ioredis (v5): the subset a NestJS service uses, against a live Redis (database 3, flushed)
import Redis from "ioredis";

const PORT = 56379;

async function live(): Promise<void> {
  const events: string[] = [];
  const r = new Redis({
    host: "127.0.0.1",
    port: PORT,
    password: undefined,
    db: 3,
    lazyConnect: true,
    enableReadyCheck: true,
    maxRetriesPerRequest: 2,
    connectTimeout: 5000,
    commandTimeout: 2000,
  });
  r.on("connect", () => events.push("connect"));
  r.on("ready", () => events.push("ready"));
  r.on("error", (err: Error) => events.push(`error ${err.message}`));
  r.on("end", () => events.push("end"));
  console.log("status", r.status);
  await r.connect();
  console.log("status", r.status);
  console.log("flushdb", await r.flushdb());
  console.log("set EX", await r.set("s:1", JSON.stringify({ a: 1, b: "x" }), "EX", 60));
  console.log("get", await r.get("s:1"), await r.get("missing"));
  console.log("ttl", await r.ttl("s:1"), await r.ttl("missing"));
  console.log("exists", await r.exists("s:1"), await r.exists("missing"));
  console.log("set PX NX", await r.set("lock", "123", "PX", 5000, "NX"), await r.set("lock", "456", "PX", 5000, "NX"));
  console.log("set plain", await r.set("plain", "v"), await r.ttl("plain"));
  console.log("set number", await r.set("n", 42), await r.get("n"));
  console.log("del", await r.del("s:1"), await r.del("s:1"), await r.del("lock", "plain"));
  // hashes, sorted sets and sets, through a pipeline
  const fields: Record<string, string | number> = { v: "1.2.3", ts: 1700000000000, ip: "" };
  const res = await r.pipeline().hset("hb:A", fields).zadd("seen", 100, "A").zadd("seen", 200, "B").sadd("dirty", "A").sadd("dirty", "B").exec();
  for (const [err, value] of res ?? []) {
    console.log("pipeline", err === null, JSON.stringify(value));
  }
  const [v, ts, none] = await r.hmget("hb:A", "v", "ts", "none");
  console.log("hmget", v, ts, none);
  const names = ["v", "ip"];
  const p = r.pipeline();
  p.hmget("hb:A", ...names);
  p.hmget("hb:B", ...names);
  p.get("missing");
  const out = (await p.exec()) ?? [];
  for (let i = 0; i < out.length; i++) {
    const [err, values] = out[i] ?? [null, null];
    console.log("pipeline item", i, err === null, JSON.stringify(values));
  }
  console.log("zrange", JSON.stringify(await r.zrange("seen", 0, -1, "WITHSCORES")), JSON.stringify(await r.zrange("seen", 0, -1)));
  console.log("zrangebyscore", JSON.stringify(await r.zrangebyscore("seen", 0, 150)));
  const q = r.pipeline();
  q.zremrangebyscore("seen", 0, 150);
  q.del("hb:A");
  q.srem("dirty", "A");
  const removed = (await q.exec()) ?? [];
  console.log("cleanup", removed.length, removed[0][0] === null, JSON.stringify(removed[0][1]), JSON.stringify(removed[2][1]));
  // a refused command in a pipeline: [ReplyError], the others still run
  const mixed = (await r.pipeline().set("str", "x").incr("str").get("str").exec()) ?? [];
  for (const [err, value] of mixed) {
    console.log("mixed", err === null ? "ok" : `${err.name}: ${err.message}`, err === null ? JSON.stringify(value) : "");
  }
  console.log("spop", JSON.stringify(await r.spop("dirty", 5)), JSON.stringify(await r.spop("dirty", 5)));
  // a server error
  try {
    await r.hmget("n", "x");
  } catch (e) {
    console.log("server error:", (e as Error).message);
  }
  // a command that gets no reply in time: another connection pauses the server
  const other = new Redis(`redis://127.0.0.1:${PORT}/3`);
  console.log("pause", await other.call("CLIENT", "PAUSE", "2500", "ALL"));
  const t0 = Date.now();
  try {
    await r.get("s:2");
    console.log("no timeout");
  } catch (e) {
    const ms = Date.now() - t0;
    console.log("timeout:", (e as Error).message, ms >= 1900 && ms < 2400);
  }
  await new Promise((resolve) => setTimeout(resolve, 700));
  console.log("after the pause", await r.set("s:3", "ok", "EX", 10), await r.get("s:3"));
  console.log("quit", await other.quit());
  console.log("quit", await r.quit());
  await new Promise((resolve) => setTimeout(resolve, 50));
  console.log("status", r.status);
  console.log("events", events.join(" | "));
}

async function down(): Promise<void> {
  const errors: string[] = [];
  const d = new Redis({ host: "127.0.0.1", port: 56398, lazyConnect: true, maxRetriesPerRequest: 2, connectTimeout: 5000, commandTimeout: 2000 });
  d.on("error", (err: Error) => {
    if (errors.indexOf(err.message) < 0) errors.push(err.message);
  });
  try {
    await d.connect();
    console.log("down: connected?");
  } catch (e) {
    console.log("down: connect:", (e as Error).message);
  }
  try {
    await d.get("k");
  } catch (e) {
    console.log("down: get:", (e as Error).message);
  }
  try {
    await d.set("k", "1", "EX", 5);
  } catch (e) {
    console.log("down: set:", (e as Error).message);
  }
  await d.quit().catch(() => undefined);
  console.log("down: status", d.status);
  console.log("down: errors", errors.join(" | "));
}

async function main(): Promise<void> {
  await live();
  await down();
}
main();
