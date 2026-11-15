// expect: runtime error: TypeError: the value is not Ok
// a variable a closure assigns, narrowed by a test: TypeScript keeps the narrowing across a call of that closure (it does
// not look into the call); the read is checked at run time (TypeScript's program would read undefined here)
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; message: string };
type R = Ok | Err;
function mk(ok: boolean): R { return ok ? { kind: "ok", value: 42 } : { kind: "err", message: "boom" }; }
function h(): void {
  let r: R = mk(true);
  const flip = (): void => { r = mk(false); };
  if (r.kind === "ok") {
    console.log("before", r.value);
    flip();
    console.log(r.value);
  }
}
h();
