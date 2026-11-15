// expect: 'value' is not a field of every member of Ok | Err
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; msg: string };
type Res = Ok | Err;
class Box { data: Res = { kind: "ok", value: 1 }; }
class Svc {
  box: Box = new Box();
  run(): number[] {
    if (this.box.data.kind !== "ok") return [];
    return [1, 2].map((n) => n + this.box.data.value);
  }
}
console.log(new Svc().run());
