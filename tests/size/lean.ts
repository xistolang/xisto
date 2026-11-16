// What a program does not use is not linked (tests/run.sh "lean"): a sequential .map and a string's length link no
// task of the parallel runtime; a small function that can fail, called twice from code that runs once, is called
// (no copy per call); a class that is never printed has an empty print function.
class Opts {
  a: string = "x";
  b: number = 1;
}
function check(n: number, max: number): number {
  if (n > max) throw new Error(`out of range: ${n}`);
  return n;
}
const o = new Opts();
const s = "ação";
console.log(s.length, [1, 2, 3].map((x: number): number => x * 2).join(","), o.a, o.b);
console.log(check(1, 5) + check(2, 5));
