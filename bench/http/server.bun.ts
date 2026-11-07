// REST server on Bun (Bun.serve). Same routes as server.xisto.ts.
Bun.serve({
  port: 8080,
  async fetch(req) {
    const url = new URL(req.url);
    if (req.method === "GET" && url.pathname === "/ping") {
      return new Response("pong", { headers: { "Content-Type": "text/plain; charset=utf-8" } });
    }
    if (req.method === "GET" && url.pathname === "/hello") {
      const name = url.searchParams.get("name") ?? "";
      return new Response(`{"hello":"${name}","len":${name.length}}`, { headers: { "Content-Type": "application/json" } });
    }
    if (req.method === "POST" && url.pathname === "/echo") {
      return new Response(await req.text(), { headers: { "Content-Type": "text/plain; charset=utf-8" } });
    }
    return new Response("not found", { status: 404 });
  },
});
console.error("bun: listening on http://127.0.0.1:8080");
