// int semantics: 64-bit, literals adapt, division yields number.
const a: int = 7;
const b: int = 2;
let c: int = a * b + 1;
c -= 3;
c++;
console.log(a, b, c, a % b, -a % b, a / b, Math.trunc(a / b), Number(a) / 2);
console.log(Math.abs(-a), Math.min(a, b), Math.max(a, b), Math.floor(2.7), Math.ceil(2.1), Math.round(2.5), Math.round(-2.5));
const big: int = 4503599627370496;
console.log(big, big + 1, 1e3, 1000, 0x10);
const ks: int[] = new Array<int>(4).fill(0);
for (let i: int = 0; i < ks.length; i++) {
  ks[i] = i * i;
}
ks[3] += 100;
ks[0]++;
console.log(ks, ks.length, ks[1] === 1, 3 === 3, 3 < 4);
function half(n: int): number {
  return n / 2;
}
function twice(n: int): int {
  return n * 2;
}
console.log(half(5), twice(21), [1, 2, 3], [1.5, 2]);
