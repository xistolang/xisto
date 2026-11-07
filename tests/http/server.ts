import { setTimeout } from "node:timers/promises";

function echo(req: HttpRequest): HttpResponse {
  return HttpResponse.json(`{"method":"${req.method}","token":"${req.header("X-Token")}","body":${req.body}}`);
}

async function slow(req: HttpRequest): Promise<HttpResponse> {
  const ms: int = Math.trunc(Number(req.query("ms")));
  await setTimeout(ms);
  return HttpResponse.text(`slept ${ms}`);
}

// a mailbox: /wait parks until /publish sends a message
const inbox: Channel<string> = new Channel<string>(0);

async function wait(req: HttpRequest): Promise<HttpResponse> {
  const msg: string = await inbox.receive();
  return HttpResponse.text(`got ${msg}`);
}

async function publish(req: HttpRequest): Promise<HttpResponse> {
  await inbox.send(req.body);
  return HttpResponse.text("sent");
}

const app: Server = new Server();
app.get("/wait", wait);
app.post("/publish", publish);
app.post("/echo", echo);
app.get("/slow", slow);
app.listen(8080);
