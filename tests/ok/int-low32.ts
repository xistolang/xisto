// an integer sum or difference that only feeds a 32-bit operator (>>, |, &, ^, <<, >>>) is computed modulo
// 2^32 without an overflow check (emit.c, emit_u32): the low bits are those of the exact result
const big: int = 4503599627370495; // 2^52 - 1: every sum below stays exact as a double too
const small: int = -4503599627370495;
const ks: int[] = [0, 1, 7, 255, 65535, 2147483647, 2147483648, 4294967295, 4294967296];
let acc: int = 0;
for (let i = 0; i < ks.length; i++) {
  const k = ks[i];
  acc += ((big + k) >> 1) ^ ((small - k) | 0);
  acc += ((k + 1) >>> 3) + ((k - big) & 0xffff) + ((k + k + k) << 2);
  console.log(k, (big + 1 - k) >> 1, (k - 1) | 0, (k + 2147483647) >>> 0, (big - k) & 0xff);
}
console.log("acc", acc);
let p0: int = 11;
let flips: int = 0;
while (p0 > 1) {
  const half = (p0 + 1) >> 1; // as fannkuch's reversal
  flips += half;
  p0 = half - 1;
}
console.log("halves", flips);
