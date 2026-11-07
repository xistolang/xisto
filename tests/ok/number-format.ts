// Number::toString: the shortest digits that read back as the same number (binary search over the digit count
// where the interval is symmetric), and the ties at powers of two, whose interval is narrower below
const out: string[] = [];
for (let e = -1074; e <= 1023; e += 7) out.push(`${Math.pow(2, e)} ${-Math.pow(2, e + 3)}`);
let x = 0.1;
for (let i = 0; i < 300; i++) {
  x = (x * 1.0000001 + 0.3333) % 1000;
  out.push(`${x} ${x / 3} ${1 / (x + 1)} ${x * 1e-7} ${x * 1e21} ${i / 7}`);
}
console.log(out.join("\n"));
console.log(JSON.stringify([0.1, 1 / 3, 5.960464477539063e-8, 1e21, 1e-7, 123456789012345680000]));
