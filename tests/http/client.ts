interface Echo {
  method: string;
  token: string;
  body: Item;
}
interface Item {
  id: int;
  name: string;
}

const item: Item = { id: 7, name: "xisto" };
const r: Response = await fetch("http://127.0.0.1:8080/echo", {
  method: "POST",
  headers: { "Content-Type": "application/json", "X-Token": "abc123" },
  body: JSON.stringify(item),
});
const e: Echo = JSON.parse(await r.text());
console.log(r.status, e.method, e.token, e.body.id, e.body.name);
// a body of bytes (a Buffer or Uint8Array), sent as they are; the answer's bytes and URL
const rb: Response = await fetch("http://127.0.0.1:8080/echo", { method: "POST", headers: { "X-Token": "bytes" }, body: Buffer.from(JSON.stringify(item)) });
const eb: Echo = JSON.parse(Buffer.from(await rb.arrayBuffer()).toString("utf8"));
console.log("bytes body:", rb.status, eb.token, eb.body.name, rb.url);
await rb.body?.cancel(); // (read whole already: nothing to cancel)
try {
  const late: Response = await fetch("http://127.0.0.1:8080/slow?ms=600", { signal: AbortSignal.timeout(100) });
  console.log("no timeout?", late.status);
} catch (err) {
  console.log("timeout:", err.message, err.name);
}
// an AbortController aborted by a timer inside a handler (tests/http/abort.ts)
const ab: Response = await fetch("http://127.0.0.1:8080/abortfetch");
console.log(await ab.text());
const early = new AbortController();
early.abort();
try {
  await fetch("http://127.0.0.1:8080/slow?ms=1", { signal: early.signal });
} catch (err) {
  console.log("aborted before:", err.name);
}
const calm = new AbortController();
const r3: Response = await fetch("http://127.0.0.1:8080/slow?ms=10", { signal: calm.signal });
console.log("not aborted:", r3.status, await r3.text(), calm.signal.aborted);
const ok: Response = await fetch("http://127.0.0.1:8080/slow?ms=20", { signal: AbortSignal.timeout(2000) });
console.log(ok.status, await ok.text());
