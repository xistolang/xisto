// The same todo API on Node (node:http), hand-written the idiomatic way.
import { createServer, IncomingMessage } from "node:http";

type Todo = { id: number; title: string; done: boolean };
const items: Todo[] = [];
let nextId = 1;

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve) => {
    const chunks: Buffer[] = [];
    req.on("data", (c: Buffer) => chunks.push(c));
    req.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
  });
}

function parseId(s: string): number {
  const n = Number(s);
  return Number.isNaN(n) || !Number.isInteger(n) || n < 1 ? -1 : n;
}

const server = createServer(async (req, res) => {
  const url = new URL(req.url ?? "/", "http://localhost");
  const send = (status: number, body: string, type = "application/json") => {
    res.writeHead(status, { "Content-Type": type });
    res.end(body);
  };
  const m = url.pathname.match(/^\/todos(?:\/([^/]+))?$/);
  if (!m) return send(404, "not found", "text/plain");
  const id = m[1] === undefined ? null : parseId(m[1]);
  try {
    if (id === null && req.method === "GET") return send(200, JSON.stringify(items));
    if (id === null && req.method === "POST") {
      const input = JSON.parse(await readBody(req));
      if (typeof input.title !== "string") return send(400, '{"error":"JSON: \'title\' must be a string"}');
      if (input.title.trim().length === 0) return send(400, '{"error":"title is required"}');
      const t: Todo = { id: nextId++, title: input.title.trim(), done: false };
      items.push(t);
      return send(201, JSON.stringify(t));
    }
    const i = items.findIndex((t) => t.id === id);
    if (req.method === "GET") return i < 0 ? send(404, '{"error":"not found"}') : send(200, JSON.stringify(items[i]));
    if (req.method === "PUT") {
      if (i < 0) return send(404, '{"error":"not found"}');
      const input = JSON.parse(await readBody(req));
      if (typeof input.title !== "string" || typeof input.done !== "boolean") return send(400, '{"error":"JSON: bad fields"}');
      if (input.title.length > 0) items[i].title = input.title;
      items[i].done = input.done;
      return send(200, JSON.stringify(items[i]));
    }
    if (req.method === "DELETE") {
      if (i < 0) return send(404, '{"error":"not found"}');
      items.splice(i, 1);
      return send(204, "");
    }
    return send(405, "method not allowed", "text/plain");
  } catch (e) {
    return send(400, JSON.stringify({ error: "JSON: " + (e as Error).message }));
  }
});
server.listen(8080, () => console.error("node: listening on http://127.0.0.1:8080"));
