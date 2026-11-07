// expect: cannot call a method on string | number
function f(v: string | number): string {
  return v.toString();
}
