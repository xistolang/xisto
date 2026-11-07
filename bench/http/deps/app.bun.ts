const items = Array.from({ length: 50 }, (_, i) => ({ id: i + 1, title: `item ${i + 1}` }));
async function query(ms: string) {
  const r = await fetch(`http://127.0.0.1:8081/q?ms=${ms}`);
  return (await r.json()) as { rows: { id: number; name: string }[]; ms: number };
}
const json = (body: string) => new Response(body, { headers: { "Content-Type": "application/json" } });
Bun.serve({
  port: 8080,
  async fetch(req) {
    const url = new URL(req.url);
    const ms = url.searchParams.get("ms") ?? "0";
    if (url.pathname === "/one") return json(JSON.stringify(await query(ms)));
    if (url.pathname === "/three") {
      const a = await query(ms); const b = await query(ms); const c = await query(ms);
      return json(`{"total":${a.rows.length + b.rows.length + c.rows.length}}`);
    }
    if (url.pathname === "/mixed") { const r = await query(ms); return json(`{"db":${r.rows.length},"items":${JSON.stringify(items)}}`); }
    return new Response("not found", { status: 404 });
  },
});
console.error("bun: listening");
