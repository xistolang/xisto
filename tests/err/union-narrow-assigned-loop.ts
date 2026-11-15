// expect: 'value' is not a field of every member of Ok | Err
// assigned later in a loop body: the union from the loop's start (the assignment flows back to the top)
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; message: string };
type R = Ok | Err;
function mk(ok: boolean): R { return ok ? { kind: "ok", value: 42 } : { kind: "err", message: "boom" }; }
let r: R = mk(true);
if (r.kind === "ok") {
  for (let i = 0; i < 2; i++) {
    console.log(r.value);
    r = mk(false);
  }
}
