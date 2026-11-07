// expect: xisto: 4 errors
// every statement of a function is checked on its own: independent errors are all reported, uses of a
// failed declaration are not
function handler(id: number): string {
  const user = findUsr(id);
  console.log(user.name);
  const n: number = "x";
  let total = 0;
  total += n;
  for (const k of [1, 2]) {
    total += k.foo;
  }
  return total.toFixed(2) + missing;
}
console.log(handler(1));
