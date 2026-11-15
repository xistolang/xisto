// expect: B has no member 'x'
type A = { kind: "a"; x: number };
type B = { kind: "b"; res: string };
function f(m: A | B): number {
  if (m.kind === "a") return 0;
  return m.x;
}
console.log(f({ kind: "a", x: 1 }));
