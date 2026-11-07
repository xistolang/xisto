// expect: 'length' may be null
// a field narrowed inside an if is nullable again after it
interface O { s?: string; }
function f(o: O): int {
  if (o.s !== null) { console.log(o.s.length); }
  return o.s.length;
}
console.log(f({ s: "x" }));
