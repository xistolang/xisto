Bun.serve({
  port: 8443,
  tls: { cert: Bun.file("tests/tls/cert.pem"), key: Bun.file("tests/tls/key.pem") },
  fetch(req) {
    const url = new URL(req.url);
    return new Response(`{"secure":true,"name":"${url.searchParams.get("name")}"}`, { headers: { "Content-Type": "application/json" } });
  },
});
