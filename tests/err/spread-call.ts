// expect: spreading arguments into a call is not supported
function sum(xs: number[]): number {
  let t = 0;
  for (const x of xs) t += x;
  return t;
}
const a = [1, 2];
console.log(sum(...a));
