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
tryIt("-max-1", () => -MAX - 1);
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
