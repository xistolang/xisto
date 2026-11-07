// BigInt: literals, + - * / % **, comparisons, conversions, printing; division checked against q * b + r === a
let a = 123456789012345678901234567890n;
const b = 987654321n;
console.log(a, b, a + b, a - b, b - a, a * b, a / b, a % b, -a / b, -a % b);
let f = 1n;
for (let i = 1; i <= 30; i++) f *= BigInt(i);
console.log(f.toString(), String(f), `f=${f}`);
const p = f * f * f;
console.log(p / (f * f) === f, p % f, p / 7n, (p + 1n) / -13n);
console.log(a < b, a > b, a === a, b !== b, a <= a, -b >= b);
console.log(Number(b), Number(f), Number(-f), typeof a);
console.log(BigInt("  -42 "), BigInt("0xff"), 0x10n, 0b101n, 0o17n, BigInt(1e20), BigInt(-7), 18446744073709551616n);
a -= 1n;
console.log(a, 2n ** 0n === 1n);
console.log(3n ** 100n, (-2n) ** 63n, (-1n) ** 3n, 0n ** 5n, 1n ** 99n, (-1n) ** 4n);

// random numbers of 1 to 40 limbs, divided by random numbers of 1 to 20 limbs; digits with long runs of 9s and 0s
// make the rare corrections of the quotient digit happen
let seed: int = 12345; // (products below 2^53: the same in JS numbers)
function rnd(n: int): int { seed = (seed * 48271) % 2147483647; return seed % n; }
function digits(n: int): string {
  let s = String(1 + rnd(9));
  for (let i = 1; i < n; i++) { const r = rnd(10); s += r < 3 ? "9" : r < 5 ? "0" : String(rnd(10)); }
  return s;
}
let bad = 0;
let sum = 0n;
for (let t = 0; t < 400; t++) {
  const x = BigInt(digits(1 + rnd(760))) * (rnd(2) === 0 ? 1n : -1n);
  const y = BigInt(digits(1 + rnd(380))) * (rnd(3) === 0 ? -1n : 1n);
  const q = x / y, r = x % y;
  if (q * y + r !== x) bad++;
  if ((r < 0n ? -r : r) >= (y < 0n ? -y : y)) bad++;
  sum += q + r;
}
console.log("division checks failed:", bad);
console.log(sum % 1000000007n, sum);
// the same length on both sides (the one-limb quotient path), near the corrections: digits close to each other
let bad2 = 0;
let sum2 = 0n;
for (let t = 0; t < 600; t++) {
  const len = 20 + rnd(400);
  const base = BigInt(digits(len));
  const x = base * BigInt(1 + rnd(9)) + BigInt(rnd(1000)) - 500n;
  const y = rnd(4) === 0 ? base : base + BigInt(rnd(3)) - 1n;
  const q = x / y, r = x % y;
  if (q * y + r !== x || r < 0n || r >= y) bad2++;
  sum2 += q * 1000n + r % 997n;
}
console.log("same-length checks failed:", bad2, sum2);
