// build: --tsc-checked
// appa round 10: a union where one of its members goes (narrowed by a helper tsc follows), checked at run time
function takeS(s: string): number { return s.length; }
function isS(v: string | number): boolean { return typeof v === "string"; }
const vs: (string | number)[] = ["abc", 4];
for (const v of vs) {
  if (isS(v)) console.log(takeS(v));
  else console.log("n");
}
const w: string | number = "x";
try { const n: number = (vs[1] as string | number); console.log(n + 1); } catch (e) { console.log("TypeError"); }
function viaHelper(v: string | number): number { return isS(v) ? 0 : v * 2; }
console.log(viaHelper(21), viaHelper("q"));
const cmp: string | number = "b";
console.log(cmp === "b");
// [date, text, n]: an untyped array of mixed kinds is an unknown[] under --tsc-checked
const binds = [new Date(0), "x", 3];
console.log(binds.length, typeof binds[1], binds[2]);
