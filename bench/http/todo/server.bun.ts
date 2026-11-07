// The same todo API on Bun (Bun.serve), hand-written the idiomatic way.
type Todo = { id: number; title: string; done: boolean };
const items: Todo[] = [];
let nextId = 1;
const json = (status: number, body: string) => new Response(body, { status, headers: { "Content-Type": "application/json" } });
const parseId = (s: string) => { const n = Number(s); return Number.isNaN(n) || !Number.isInteger(n) || n < 1 ? -1 : n; };

Bun.serve({
  port: 8080,
  async fetch(req) {
    const url = new URL(req.url);
    const m = url.pathname.match(/^\/todos(?:\/([^/]+))?$/);
    if (!m) return new Response("not found", { status: 404 });
    const id = m[1] === undefined ? null : parseId(m[1]);
    try {
      if (id === null && req.method === "GET") return json(200, JSON.stringify(items));
      if (id === null && req.method === "POST") {
        const input = JSON.parse(await req.text());
        if (typeof input.title !== "string") return json(400, '{"error":"JSON: \'title\' must be a string"}');
        if (input.title.trim().length === 0) return json(400, '{"error":"title is required"}');
        const t: Todo = { id: nextId++, title: input.title.trim(), done: false };
        items.push(t);
        return json(201, JSON.stringify(t));
      }
      const i = items.findIndex((t) => t.id === id);
      if (req.method === "GET") return i < 0 ? json(404, '{"error":"not found"}') : json(200, JSON.stringify(items[i]));
      if (req.method === "PUT") {
        if (i < 0) return json(404, '{"error":"not found"}');
        const input = JSON.parse(await req.text());
        if (typeof input.title !== "string" || typeof input.done !== "boolean") return json(400, '{"error":"JSON: bad fields"}');
        if (input.title.length > 0) items[i].title = input.title;
        items[i].done = input.done;
        return json(200, JSON.stringify(items[i]));
      }
      if (req.method === "DELETE") {
        if (i < 0) return json(404, '{"error":"not found"}');
        items.splice(i, 1);
        return new Response("", { status: 204 });
      }
      return new Response("method not allowed", { status: 405 });
    } catch (e) {
      return json(400, JSON.stringify({ error: "JSON: " + (e as Error).message }));
    }
  },
});
console.error("bun: listening on http://127.0.0.1:8080");
