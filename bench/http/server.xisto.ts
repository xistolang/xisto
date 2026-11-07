// REST server in Xisto. Same routes as server.node.ts and server.bun.ts.
function ping(req: HttpRequest): HttpResponse {
  return HttpResponse.text("pong");
}

function hello(req: HttpRequest): HttpResponse {
  const name: string = req.query("name");
  return HttpResponse.json("{\"hello\":\"" + name + "\",\"len\":" + String(name.length) + "}");
}

function echo(req: HttpRequest): HttpResponse {
  return HttpResponse.text(req.body);
}

const app: Server = new Server();
app.get("/ping", ping);
app.get("/hello", hello);
app.post("/echo", echo);
app.listen(8080);
