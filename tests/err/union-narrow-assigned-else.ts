// expect: 'value' is not a field of every member of Ok | Err
// assigned in the else branch: the union after the if
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; message: string };
type R = Ok | Err;
function mk(ok: boolean): R { return ok ? { kind: "ok", value: 42 } : { kind: "err", message: "boom" }; }
let r: R = mk(true);
if (r.kind === "ok") {
  if (Date.now() < 0) { console.log("then", r.value); } else { r = mk(false); }
  console.log(r.value);
}
