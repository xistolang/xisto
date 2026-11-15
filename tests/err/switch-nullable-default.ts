// expect: 'length' may be null
function f(x: string | null): number {
  switch (x) {
    case "a": return x.length;
    default: return x.length;
  }
}
console.log(f("a"));
