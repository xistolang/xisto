function f(a: int[]): int {
  return a[5];
}
function g(): int {
  return f([1]);
}
console.log(g());
