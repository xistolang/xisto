// expect: 'length' may be null
function f(x: string | null): number {
  switch (x) {
    case null:
    case "a": return x.length;
  }
  return 0;
}
console.log(f("a"));
