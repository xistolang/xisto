// expect: 'length' may be null
// assigning something that may be null undoes the narrowing
interface O { s?: string; }
function g(): string | null { return null; }
function f(o: O): int {
  if (o.s !== null) {
    o.s = g();
    return o.s.length;
  }
  return 0;
}
console.log(f({ s: "x" }));
