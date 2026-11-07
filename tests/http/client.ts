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
try {
  const late: Response = await fetch("http://127.0.0.1:8080/slow?ms=600", { signal: AbortSignal.timeout(100) });
  console.log("no timeout?", late.status);
} catch (err) {
  console.log("timeout:", err.message);
}
const ok: Response = await fetch("http://127.0.0.1:8080/slow?ms=20", { signal: AbortSignal.timeout(2000) });
console.log(ok.status, await ok.text());
