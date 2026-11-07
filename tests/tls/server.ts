function hello(req: HttpRequest): HttpResponse {
  return HttpResponse.json(`{"secure":true,"name":"${req.query("name")}"}`);
}
function big(req: HttpRequest): HttpResponse {
  return HttpResponse.text("x".repeat(200000));
}
function huge(req: HttpRequest): HttpResponse {
  return HttpResponse.text("x".repeat(20000000));
}
function stream(req: HttpRequest): HttpResponse {
  return HttpResponse.stream(lines("tests/tls/cert.pem"));
}
const app: Server = new Server();
app.get("/hello", hello);
app.get("/big", big);
app.get("/stream", stream);
app.get("/huge", huge);
app.listen(8443, { cert: "tests/tls/cert.pem", key: "tests/tls/key.pem" });
