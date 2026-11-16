// a name declared twice in a function (two blocks, a closure's parameter) where one declaration is a number: the
// int inference of `let t = 0` reads it as the number it may be (it was refused: int where a number goes)
function f(xs: number[]): number {
  let t = 0;
  { const j = 1; t += j; }
  { const j = xs[0]; t += j; }
  return t;
}
function g(xs: number[]): number {
  let t = 0;
  for (let i = 0; i < 2; i++) t += i;
  xs.forEach((i) => { t += i; });
  return t;
}
function h(a: int[], b: number[]): number {
  let t = 0;
  { const v = a; t += v[0]; }
  { const v = b; t += v[0]; }
  let u = 0;
  for (const x of a) u += x;
  for (const x of b) u += x;
  return t + u;
}
function k(): number {
  let t = 0;
  { let s = 0; s++; t += s; }
  { let s = 0; s += 0.5; t += s; }
  return t;
}
console.log(f([0.5]), g([0.25]), h([1], [0.5]), k());
