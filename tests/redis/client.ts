import { createClient } from "redis";

async function main(): Promise<void> {
  const url = process.env.get("REDIS_TEST_URL");
  const client = createClient({ url: url || "redis://127.0.0.1:56379/2" });
  client.on("error", (err: Error): void => console.log("redis error", err.message));
  await client.connect();
  console.log("ping", await client.ping());
  console.log("flush", await client.flushDb());
  console.log("get missing", await client.get("k"));
  console.log("set", await client.set("k", "olá, mundo", { EX: 60 }));
  console.log("get", await client.get("k"));
  console.log("ttl>0", (await client.ttl("k")) > 0);
  console.log("set NX on existing", await client.set("k", "x", { NX: true }));
  console.log("incr", await client.incr("n"), await client.incr("n"), await client.incrBy("n", 10));
  console.log("exists", await client.exists("k"), await client.exists("nope"));
  console.log("expire", await client.expire("n", 30));
  const big = "x".repeat(200000);
  await client.set("big", big);
  const back = await client.get("big");
  console.log("big roundtrip", back === big);
  console.log("keys", (await client.keys("*")).length);
  console.log("mGet", await client.mGet(["n", "nope", "k"]));
  console.log("del", await client.del("k"), await client.del("k"));
  try {
    await client.incr("big");
  } catch (e) {
    console.log("server error:", e.message);
  }
  console.log("quit", await client.quit());
  const bad = createClient({ url: "redis://127.0.0.1:1" });
  try {
    await bad.connect();
  } catch (e) {
    console.log("connect error:", e.message);
  }
}
main();
