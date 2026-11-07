import { setTimeout } from "node:timers/promises";

interface Item {
  id: int;
}
function pick(a: int[]): int {
  return a[7];
}
function boom(req: HttpRequest): HttpResponse {
  return HttpResponse.text(`${pick([1, 2])}`);
}
async function late(req: HttpRequest): Promise<HttpResponse> {
  await setTimeout(1);
  throw new Error("secret \"detail\"");
}
// a message that merely starts like a JSON error is still the program's: 500, nothing to the client
function leak(req: HttpRequest): HttpResponse {
  throw new Error("JSON-ish: db password is hunter2");
}
function item(req: HttpRequest): HttpResponse {
  const it: Item = JSON.parse(req.body);
  return HttpResponse.text(`${it.id}`);
}
const app: Server = new Server();
app.get("/boom", boom);
app.get("/late", late);
app.post("/item", item);
app.get("/leak", leak);
app.listen(8091);
