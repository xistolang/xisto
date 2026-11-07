// expect: calling a function value is not supported by the qbe backend yet
function apply(f: (x: int) => int, v: int): int {
  return f(v);
}
function inc(x: int): int {
  return x + 1;
}
console.log(apply(inc, 1));
