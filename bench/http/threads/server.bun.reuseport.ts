// Bun with reusePort: run N copies of this process to use more cores (the Bun way).
Bun.serve({
  port: 8080,
  reusePort: true,
  fetch(req) {
    const url = new URL(req.url);
    if (url.pathname === "/ping") return new Response("pong", { headers: { "Content-Type": "text/plain; charset=utf-8" } });
    if (url.pathname === "/hello") {
      const name = url.searchParams.get("name") ?? "";
      return new Response(`{"hello":"${name}","len":${name.length}}`, { headers: { "Content-Type": "application/json" } });
    }
    return new Response("not found", { status: 404 });
  },
});
console.error("bun: listening (reusePort)");
