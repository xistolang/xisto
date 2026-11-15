type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; message: string };
type R = Ok | Err;
function mk(ok: boolean): R { return ok ? { kind: "ok", value: 42 } : { kind: "err", message: "boom" }; }
let r: R = mk(true);
if (r.kind === "ok") {
  if (Date.now() < 0) { r = mk(false); } else { console.log("else still ok", r.value); }
  console.log("after", r.kind);
  if (r.kind === "ok") console.log("again", r.value); else console.log("err", r.message);
}
let s: R = mk(true);
if (s.kind === "ok") {
  console.log("before", s.value);
  for (let i = 0; i < 2; i++) {
    if (s.kind === "ok") console.log("loop ok", s.value); else console.log("loop err", s.message);
    s = mk(false);
  }
  console.log("end", s.kind);
}
let t: R = mk(true);
const flip = (): void => { t = mk(false); };
if (t.kind === "ok") {
  console.log("t before", t.value);
}
flip();
console.log("t", t.kind);
function g(x: R): string {
  let y: R = x;
  if (y.kind === "err") { y = mk(true); }
  return y.kind;
}
console.log(g(mk(false)), g(mk(true)));
