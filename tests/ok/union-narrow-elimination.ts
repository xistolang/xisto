type A = { kind: "a"; x: number };
type B = { kind: "b"; res: string };
type M = A | B;
function f1(m: M): string {
  if (m.kind === "a") return "A" + m.x;
  return m.res;
}
function f2(m: M): string {
  if (m.kind === "a") { throw new Error("no"); }
  return m.res;
}
function f3(ms: M[]): string {
  let s = "";
  for (const m of ms) {
    if (m.kind === "a") continue;
    s += m.res;
  }
  return s;
}
function f4(ms: M[]): string {
  let s = "";
  for (const m of ms) {
    if (m.kind === "b") break;
    s += String(m.x);
  }
  return s;
}
function f5(m: M): string {
  if (m.kind === "a") { return "x"; } else { console.log("else", m.res); }
  return m.res;
}
function f6(m: M): string {
  if (m.kind !== "b") return "na";
  return m.res;
}
console.log(f1({ kind: "a", x: 1 }), f1({ kind: "b", res: "r" }));
console.log(f2({ kind: "b", res: "r2" }));
console.log(f3([{ kind: "a", x: 1 }, { kind: "b", res: "q" }]));
console.log(f4([{ kind: "a", x: 1 }, { kind: "b", res: "q" }]));
console.log(f5({ kind: "b", res: "r5" }), f6({ kind: "b", res: "r6" }));
