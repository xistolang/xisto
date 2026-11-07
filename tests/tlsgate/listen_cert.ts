function hi(req: HttpRequest): HttpResponse {
  return HttpResponse.text("hi");
}
const app: Server = new Server();
app.get("/", hi);
app.listen(8443, { cert: "cert.pem", key: "key.pem" });
