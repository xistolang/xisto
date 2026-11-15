// BigInt(x) of numbers, strings and booleans; toString(radix); BigInt.asUintN / asIntN; typeof
console.log(BigInt(10), BigInt(-3), BigInt(2 ** 60), BigInt("0x1f"), BigInt("0b101"), BigInt("0o17"), BigInt(" 123 "), BigInt("-42"));
console.log(BigInt(true), BigInt(false), BigInt(""));
for (const bad of ["1.5", "zz", "12n"]) {
  try {
    BigInt(bad);
  } catch (e) {
    console.log((e as Error).name, (e as Error).message);
  }
}
try {
  BigInt(1.5);
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
const big = 2n ** 100n + 12345n;
console.log((255n).toString(16), (-255n).toString(2), big.toString(36), big.toString(16), big.toString(10), (0n).toString(7));
console.log((10n ** 40n).toString(2).length, (-(10n ** 20n)).toString(36), (35n).toString(36));
try {
  big.toString(37);
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
console.log(BigInt.asUintN(8, 257n), BigInt.asIntN(8, 255n), BigInt.asIntN(8, 127n), BigInt.asIntN(8, -129n));
console.log(BigInt.asUintN(64, -1n), BigInt.asIntN(64, 2n ** 63n), BigInt.asIntN(0, 5n), BigInt.asUintN(1, 3n));
const values: unknown[] = [1n, 1, "1"];
console.log(values.map((v) => typeof v), typeof big === "bigint");
