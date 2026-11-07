// expect: may reach its end without returning
function f(n: number): number {
  if (n > 0) {
    return n;
  }
}
