type Ok = { kind: "ok"; value: number };
type Err = { kind: "err"; msg: string };
type Res = Ok | Err;
type Wrap = { tag: string; inner: Res };
class Holder { w: Wrap; constructor(w: Wrap) { this.w = w; } }

function deep(h: Holder): string {
  if (h.w.inner.kind === "err") return "deep err " + h.w.inner.msg;
  return "deep ok " + h.w.inner.value * 2;
}
console.log(deep(new Holder({ tag: "a", inner: { kind: "ok", value: 21 } })));
console.log(deep(new Holder({ tag: "b", inner: { kind: "err", msg: "boom" } })));

function reassigned(h: Holder, other: Res): string {
  let out = "";
  if (h.w.inner.kind === "ok") {
    out += "before " + h.w.inner.value + "; ";
    h.w.inner = other;
    if (h.w.inner.kind === "err") out += "after err " + h.w.inner.msg;
    else out += "after ok " + h.w.inner.value;
  }
  return out;
}
console.log(reassigned(new Holder({ tag: "c", inner: { kind: "ok", value: 1 } }), { kind: "err", msg: "now err" }));

function prefix(h: Holder, w2: Wrap): string {
  if (h.w.inner.kind !== "ok") return "not ok";
  const v = h.w.inner.value;
  h.w = w2;
  switch (h.w.inner.kind) {
    case "ok": return "was " + v + ", now ok " + h.w.inner.value;
    case "err": return "was " + v + ", now err " + h.w.inner.msg;
  }
  return "unreachable";
}
console.log(prefix(new Holder({ tag: "d", inner: { kind: "ok", value: 5 } }), { tag: "e", inner: { kind: "err", msg: "x" } }));

class Machine {
  state: Res = { kind: "err", msg: "idle" };
  log: string[] = [];
  note(s: string): void { this.log.push(s); }
  step(): string {
    if (this.state.kind === "err") {
      this.note("err seen");
      return "error: " + this.state.msg;
    } else {
      this.note("ok seen");
    }
    this.note("value read");
    return "value: " + this.state.value;
  }
  bump(): void {
    if (this.state.kind === "ok") this.state = { kind: "ok", value: this.state.value + 1 };
    else this.state = { kind: "ok", value: 0 };
  }
}
const m = new Machine();
console.log(m.step());
m.bump();
m.bump();
console.log(m.step(), m.log.join(","));

const pairs: [Res, string][] = [[{ kind: "ok", value: 3 }, "three"], [{ kind: "err", msg: "bad" }, "bad one"]];
for (const p of pairs) {
  if (p[0].kind === "err") {
    console.log("skip", p[1], p[0].msg);
    continue;
  }
  console.log("use", p[1], p[0].value);
}
