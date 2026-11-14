// TechEmpower Framework Benchmarks: the xisto native server (the built-in Server, no framework).
// Tests: /plaintext and /json (phase 1). Rules (TFB wiki, Framework Tests Overview): Server and Date headers on every
// response, Content-Length, keep-alive, pipelining on /plaintext, and the JSON serialized per request from an
// object (no cached response). PORT selects the port (default 9100); XISTO_WORKERS the number of event loops.

interface Message {
  message: string;
}

function port(): int {
  const p: string | undefined = process.env.get("PORT");
  return p ? Math.trunc(Number(p)) : 9100;
}

function plaintext(req: HttpRequest): HttpResponse {
  return HttpResponse.text("Hello, World!").header("Server", "xisto").header("Date", new Date().toUTCString());
}

function json(req: HttpRequest): HttpResponse {
  const body: Message = { message: "Hello, World!" };
  return HttpResponse.json(JSON.stringify(body)).header("Server", "xisto").header("Date", new Date().toUTCString());
}

const app: Server = new Server();
app.get("/plaintext", plaintext);
app.get("/json", json);
app.listen(port());
