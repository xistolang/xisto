// expect: runtime error: TypeError: the value is not Ok
// a module variable a function assigns, narrowed by a test: kept across the call, as TypeScript does; the read is checked
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; message: string };
type R = Ok | Err;
function mk(ok: boolean): R { return ok ? { kind: "ok", value: 42 } : { kind: "err", message: "boom" }; }
let cur: R = mk(true);
function fail(): void { cur = mk(false); }
if (cur.kind === "ok") {
  console.log("before", cur.value);
  fail();
  console.log(cur.value);
}
