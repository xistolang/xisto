// expect: runtime error: TypeError: the value is not Ok
// a field path narrowed by a test stays narrowed across a call, as in TypeScript; when the call changed it, the read is
// checked at run time (TypeScript's program would read undefined here)
type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; msg: string };
type Res = Ok | Err;
class Machine {
  state: Res = { kind: "ok", value: 1 };
  fail(): void { this.state = { kind: "err", msg: "failed" }; }
  run(): number {
    if (this.state.kind === "ok") {
      this.fail();
      return this.state.value;
    }
    return 0;
  }
}
console.log(new Machine().run());
