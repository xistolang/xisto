// int is an integer JavaScript represents exactly: |v| <= 2^53 - 1 (Number.MAX_SAFE_INTEGER). Inside the range
// int arithmetic is exact as in Node; a result outside is a runtime error, never a different number from the one
// Node would print (XB-075). The expected output is checked in: past the range Node rounds and goes on.
const MAX: int = 9007199254740991;
console.log(MAX, MAX - 1, -MAX, MAX - MAX);
function tryIt(name: string, f: () => int): void {
  try {
    console.log(name, f());
  } catch (e) {
    console.log(name, "->", (e as Error).message.startsWith("integer overflow") ? "integer overflow" : (e as Error).message);
  }
}
tryIt("max+1", () => MAX + 1);
tryIt("-max-1", () => -MAX - 1); // -2^53 is exact in a double: Node gives the same number, allowed
tryIt("-max-2", () => -MAX - 2);
tryIt("max-(-1)", () => MAX - (-1));
let m: int = 94906267;
tryIt("m*m", () => m * m); // 9007199515875289 > 2^53: Node prints 9007199515875288
let n: int = 94906265;
tryIt("n*n", () => n * n); // 9007199136250225 < 2^53: exact
let two: int = 2;
tryIt("2**53", () => two ** 53);
tryIt("2**52", () => two ** 52);
let k: int = MAX - 1;
k++;
console.log("k", k);
tryIt("k++", () => { k++; return k; });
tryIt("(max+2)|0", () => (MAX + 2) | 0); // Node: 2^53 + 1 rounds to 2^53 before the |, low bits 0
tryIt("trunc", () => Math.trunc(9007199254740992.0));
tryIt("trunc ok", () => Math.trunc(-9007199254740991.0));
console.log(9007199254740993, 2 ** 53, 2 ** 60); // beyond the range a literal or a folded power is a number, as in JS
console.log((MAX | 0), (MAX >>> 0), (1 << 31), (-1 >>> 0)); // bitwise: 32 bits, as in JS
// the cheaper forms of the test: a constant multiplier, operands proven never negative
function times3(a: int): int {
  return a * 3;
}
tryIt("3*ok", () => times3(3002399751580330));
tryIt("3*hi", () => times3(3002399751580331));
tryIt("3*lo", () => times3(-3002399751580331));
function grow(step: int, n: int): int {
  let u: int = 0;
  for (let i = 0; i < n; i++) u = u + step; // u, step and i never negative: only the top is tested
  return u;
}
tryIt("grow ok", () => grow(4503599627370495, 2));
tryIt("grow hi", () => grow(4503599627370496, 2));
function gap(a: int, b: int): int {
  let x: int = a % 1000000;
  if (x < 0) x = 0;
  return x - b * 0;
}
tryIt("gap", () => gap(123456789, 5));
