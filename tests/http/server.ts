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

// pipelined requests must each see their own headers, query and route parameters, also across an await while more
// requests (one of them large) arrive on the same connection
async function who(req: HttpRequest): Promise<HttpResponse> {
  const ms: int = Math.trunc(Number(req.query("ms") ?? "0"));
  if (ms > 0) await setTimeout(ms);
  return HttpResponse.text(`who=${req.param("name")} id=${req.header("X-Id") ?? "none"} n=${req.query("n") ?? "none"};`);
}

// HttpResponse.status: a JSON body goes out as application/json, as Express's res.status(code).json(...) sends it
function byStatus(req: HttpRequest): HttpResponse {
  const kind = req.param("kind");
  if (kind === "created") return HttpResponse.status(201, JSON.stringify({ id: 1 }));
  if (kind === "missing") return HttpResponse.status(404, `{"error":"not found"}`);
  if (kind === "list") return HttpResponse.status(200, " [1, 2] ");
  return HttpResponse.status(418, "{teapot");
}

const app: Server = new Server();
app.get("/status/:kind", byStatus);
app.get("/wait", wait);
app.post("/publish", publish);
app.post("/echo", echo);
app.get("/slow", slow);
app.get("/who/:name", who);
app.get("/ctx", ctx);
app.get("/abortfetch", abortFetch);
app.get("/redir", redir);
app.post("/seeother", seeOther);
app.listen(8080);
