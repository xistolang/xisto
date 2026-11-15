// expect: 'value' is not a field of every member of Ok | Err
// a local narrowed out here, read in a callback, assigned after the callback was made
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; message: string };
type R = Ok | Err;
function mk(ok: boolean): R { return ok ? { kind: "ok", value: 42 } : { kind: "err", message: "boom" }; }

function h(): void {
  let r: R = mk(true);
  if (r.kind === "ok") {
    const f = () => { console.log(r.value); };
    r = mk(false);
    f();
  }
}
h();
