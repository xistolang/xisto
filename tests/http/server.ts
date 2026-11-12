import { setTimeout } from "node:timers/promises";
import { AsyncLocalStorage } from "node:async_hooks";
import { abortFetch } from "./abort";

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

// node:async_hooks: each request runs in its own store, which must survive its awaits while others interleave
const requestId = new AsyncLocalStorage<string>();

async function whoAmI(): Promise<string> {
  await setTimeout(5);
  return requestId.getStore() ?? "none";
}

async function ctx(req: HttpRequest): Promise<HttpResponse> {
  const id = req.query("id");
  const ms: int = Math.trunc(Number(req.query("ms")));
  return requestId.run(id, async (): Promise<HttpResponse> => {
    await setTimeout(ms);
    const seen = await whoAmI();
    return HttpResponse.text(`${id}=${requestId.getStore() ?? "none"}/${seen}`);
  });
}

// redirects that fetch follows: to a path on the same host, and a 303 after a POST (goes on as a GET)
function redir(req: HttpRequest): HttpResponse {
  return HttpResponse.status(302, "").header("Location", "/slow?ms=1");
}
function seeOther(req: HttpRequest): HttpResponse {
  return HttpResponse.status(303, "").header("Location", "http://127.0.0.1:8080/redir");
}

const app: Server = new Server();
app.get("/wait", wait);
app.post("/publish", publish);
app.post("/echo", echo);
app.get("/slow", slow);
app.get("/ctx", ctx);
app.get("/abortfetch", abortFetch);
app.get("/redir", redir);
app.post("/seeother", seeOther);
app.listen(8080);
