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
// quotients of one limb near the exact multiples (x = y * k + d, d in -2..2 and y - 1), with dividends one limb
// longer than the divisor too (a small top limb in y); the quotient alone (/) settles it from the top limbs
let bad3 = 0;
let sum3 = 0n;
const L64 = 2n ** 64n;
for (let t = 0; t < 500; t++) {
  const limbs = 2 + rnd(12);
  let y = BigInt(1 + rnd(2000000000)) * BigInt(1 + rnd(4)); // a small top limb
  for (let i = 1; i < limbs; i++) y = y * L64 + (rnd(3) === 0 ? L64 - 1n : rnd(3) === 0 ? 0n : BigInt(rnd(2000000000)) * BigInt(rnd(2000000000)));
  const k = rnd(2) === 0 ? BigInt(1 + rnd(9)) : BigInt(1 + rnd(2000000000)) * BigInt(1 + rnd(2000000000)) * 3n;
  const ds = [-2n, -1n, 0n, 1n, 2n, y - 1n];
  for (const d of ds) {
    const x = y * k + d;
    const q = x / y, r = x % y;
    const qe = d < 0n ? k - 1n : k;
    if (q !== qe || q * y + r !== x || r < 0n || r >= y) bad3++;
    sum3 += q % 1000003n + r % 1009n;
  }
}
console.log("near-multiple checks failed:", bad3, sum3);
let big1 = 1n; // additions and subtractions whose carries and borrows run through many limbs
for (let i = 0; i < 9; i++) big1 = big1 * L64;
console.log(big1 - 1n, (big1 - 1n) + 1n === big1, big1 + big1 - 1n, -(big1 - 1n) - 1n, (big1 - 1n) * (big1 - 1n) - big1 * big1 + 2n * big1);
// a + b * c and a - b * c become one call (xs_big_addmul / submul): every sign, a shorter and longer than the
// product, a one-limb factor on either side, results that cross zero or land on it
let fused = 0n;
let fusedBad = 0;
for (let t = 0; t < 400; t++) {
  const a = BigInt(digits(1 + rnd(80))) * (rnd(2) === 0 ? 1n : -1n);
  const b = BigInt(digits(1 + rnd(80))) * (rnd(2) === 0 ? 1n : -1n);
  const c = (rnd(3) === 0 ? BigInt(digits(1 + rnd(40))) : BigInt(rnd(5) === 0 ? 0 : 1 + rnd(2000000000))) * (rnd(2) === 0 ? 1n : -1n);
  const p = b * c;
  const s1 = a + b * c, s2 = a - b * c, s3 = a + c * b, s4 = p - b * c, s5 = -p + b * c;
  let acc = a;
  acc += b * c;
  let acc2 = a;
  acc2 -= c * b;
  if (s1 !== a + p || s2 !== a - p || s3 !== s1 || s4 !== 0n || s5 !== 0n || acc !== s1 || acc2 !== s2) fusedBad++;
  fused += s1 % 1000003n + s2 % 999983n + (s4 === 0n ? 1n : 0n);
}
console.log("fused checks failed:", fusedBad, fused);
console.log(5n - 2n * 3n, -5n + 2n * 3n, 6n - 2n * 3n, 0n + 7n * 0n, 0n - 7n * 9n, L64 - 1n - (L64 - 1n) * 1n, (L64 * L64) - (L64 + 1n) * (L64 - 1n));
