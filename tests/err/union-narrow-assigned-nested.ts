// expect: 'value' is not a field of every member of Ok | Err
// assigned in a nested block of the narrowed region: the union again after it (as tsc)
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; message: string };
type R = Ok | Err;
function mk(ok: boolean): R { return ok ? { kind: "ok", value: 42 } : { kind: "err", message: "boom" }; }
let r: R = mk(true);
if (r.kind === "ok") {
  if (Date.now() > 0) { r = mk(false); }
  console.log(r.value);
}
