// expect: 'value' is not a field of every member of Ok | Err
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; msg: string };
type Res = Ok | Err;
class Box { data: Res = { kind: "ok", value: 1 }; }
function f(b: Box, o: Res): number {
  if (b.data.kind === "ok") {
    b.data = o;
    return b.data.value;
  }
  return 0;
}
console.log(f(new Box(), { kind: "err", msg: "e" }));
