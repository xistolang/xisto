import { createServer } from "node:http";
import { Agent } from "undici";
const agent = new Agent({ keepAliveTimeout: 60000, connections: 1024 });
const items = Array.from({ length: 50 }, (_, i) => ({ id: i + 1, title: `item ${i + 1}` }));
async function query(ms: string) {
  const r = await fetch(`http://127.0.0.1:8081/q?ms=${ms}`, { dispatcher: agent } as any);
  return (await r.json()) as { rows: { id: number; name: string }[]; ms: number };
}
createServer(async (req, res) => {
  const url = new URL(req.url ?? "/", "http://localhost");
  const ms = url.searchParams.get("ms") ?? "0";
  const send = (body: string) => { res.writeHead(200, { "Content-Type": "application/json" }); res.end(body); };
  if (url.pathname === "/one") return send(JSON.stringify(await query(ms)));
  if (url.pathname === "/three") {
    const a = await query(ms); const b = await query(ms); const c = await query(ms);
    return send(`{"total":${a.rows.length + b.rows.length + c.rows.length}}`);
  }
  if (url.pathname === "/mixed") { const r = await query(ms); return send(`{"db":${r.rows.length},"items":${JSON.stringify(items)}}`); }
  res.writeHead(404); res.end("not found");
}).listen(8080, () => console.error("node: listening"));
