// expect: 'value' is not a field of every member of Ok | Err
// assigned in a while body: the union from the loop's start
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; message: string };
type R = Ok | Err;
function mk(ok: boolean): R { return ok ? { kind: "ok", value: 42 } : { kind: "err", message: "boom" }; }
let r: R = mk(true);
let n = 0;
while (r.kind === "ok" && n < 3) {
  n++;
  if (n === 2) r = mk(false);
}
if (r.kind === "ok") {
  let k = 0;
  while (k < 2) { k++; console.log(r.value); if (k === 1) { r = mk(false); } }
}
