type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; message: string };
type R = Ok | Err;
function mk(ok: boolean): R { return ok ? { kind: "ok", value: 42 } : { kind: "err", message: "boom" }; }
function run(): string {
  let r: R = mk(false);
  const out: string[] = [];
  r = { kind: "ok", value: 7 };
  out.push("assigned ok " + r.value);
  r = { kind: "err", message: "later" };
  out.push("assigned err " + r.message);
  for (let i = 0; i < 3; i++) {
    if (r.kind === "err") out.push("loop err " + r.message);
    else out.push("loop ok " + r.value);
    r = { kind: "ok", value: i };
    out.push("in loop " + r.value);
  }
  out.push(r.kind);
  return out.join(" | ");
}
console.log(run());
let g: R = { kind: "ok", value: 1 };
g = { kind: "ok", value: 2 };
console.log(g.value);
