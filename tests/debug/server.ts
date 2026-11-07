const port: int = Math.trunc(Number(process.argv[2]));

function hello(req: HttpRequest): HttpResponse {
  const name = req.query("name");
  const text = `hello ${name}`;
  return HttpResponse.text(text);
}

const app: Server = new Server();
app.get("/hello", hello);
app.listen(port);
