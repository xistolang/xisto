// A fake database: GET /q?ms=N answers after N milliseconds without blocking anyone else.
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
Bun.serve({
  port: 8081,
  async fetch(req) {
    const url = new URL(req.url);
    const ms = Number(url.searchParams.get("ms") ?? "0");
    if (ms > 0) await sleep(ms);
    return new Response(`{"rows":[{"id":1,"name":"isa"},{"id":2,"name":"ju"}],"ms":${ms}}`, { headers: { "Content-Type": "application/json" } });
  },
});
console.error("db: listening on 8081");
