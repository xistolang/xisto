type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; msg: string };
type Res = Ok | Err;
class Box { data: Res; constructor(d: Res) { this.data = d; } }
class Svc {
  state: Res = { kind: "err", msg: "init" };
  show(): string {
    if (this.state.kind === "ok") return "ok " + this.state.value;
    return "err " + this.state.msg;
  }
}
function g(r: Box): string {
  if (r.data.kind === "ok") return "v=" + r.data.value;
  return "m=" + r.data.msg;
}
console.log(g(new Box({ kind: "ok", value: 3 })), g(new Box({ kind: "err", msg: "bad" })));
const s = new Svc();
console.log(s.show());
s.state = { kind: "ok", value: 9 };
console.log(s.show());
type R2 = { ok: true; value: string } | { ok: false; error: string };
const t: [R2, number] = [{ ok: true, value: "hi" }, 2];
if (t[0].ok) console.log(t[0].value, t[1]);
const u: [R2, number] = [{ ok: false, error: "e" }, 3];
if (!u[0].ok) console.log(u[0].error);
function big2(b: Box): boolean { return b.data.kind === "ok" && b.data.value > 1; }
function show2(b: Box): string { return b.data.kind === "ok" ? "v" + b.data.value : "m" + b.data.msg; }
function bad2(b: Box): boolean { return b.data.kind !== "ok" || b.data.value < 0; }
const boxes = [new Box({ kind: "ok", value: 2 }), new Box({ kind: "err", msg: "no" })];
for (const b of boxes) console.log(big2(b), show2(b), bad2(b));
