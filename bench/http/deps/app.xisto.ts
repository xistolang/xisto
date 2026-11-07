// An API whose handlers wait on a slow dependency (the fake database on 8081).
class Row {
  id: int = 0;
  name: string = "";
}
class DbReply {
  rows: Row[] = [];
  ms: int = 0;
}
class Item {
  id: int;
  title: string;
  constructor(id: int, title: string) {
    this.id = id;
    this.title = title;
  }
}
const items: Item[] = [];
for (let i: int = 1; i <= 50; i++) {
  items.push(new Item(i, `item ${i}`));
}

async function query(ms: string): Promise<DbReply> {
  const r: Response = await fetch(`http://127.0.0.1:8081/q?ms=${ms}`);
  const reply: DbReply = JSON.parse(await r.text());
  return reply;
}

function one(req: HttpRequest): HttpResponse {
  const reply: DbReply = await query(req.query("ms"));
  return HttpResponse.json(JSON.stringify(reply));
}

function three(req: HttpRequest): HttpResponse {
  const ms: string = req.query("ms");
  const a: DbReply = await query(ms);
  const b: DbReply = await query(ms);
  const c: DbReply = await query(ms);
  return HttpResponse.json(`{"total":${a.rows.length + b.rows.length + c.rows.length}}`);
}

function mixed(req: HttpRequest): HttpResponse {
  const reply: DbReply = await query(req.query("ms"));
  return HttpResponse.json(`{"db":${reply.rows.length},"items":${JSON.stringify(items)}}`);
}

const app: Server = new Server();
app.get("/one", one);
app.get("/three", three);
app.get("/mixed", mixed);
app.listen(8080);
