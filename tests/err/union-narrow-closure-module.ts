// expect: 'value' is not a field of every member of Ok | Err
// a module variable narrowed out here, read in a callback that runs after it was assigned
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; message: string };
type R = Ok | Err;
function mk(ok: boolean): R { return ok ? { kind: "ok", value: 42 } : { kind: "err", message: "boom" }; }
let r: R = mk(true);
if (r.kind === "ok") {
  const f = () => { console.log(r.value); };
  r = mk(false);
  f();
}
