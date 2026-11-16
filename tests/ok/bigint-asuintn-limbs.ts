const vals: bigint[] = [0n, 1n, -1n, 5n, -5n, 127n, 128n, -128n, -129n, 255n, 256n, 18446744073709551615n, 18446744073709551616n, -18446744073709551616n, 9223372036854775807n, -9223372036854775808n, 9223372036854775808n, 123456789012345678901234567890n, -123456789012345678901234567890n];
const bitsL: number[] = [0, 1, 7, 8, 9, 63, 64, 65, 100, 128, 1.5, 8.9];
for (const b of bitsL) {
  const out: string[] = [];
  for (const v of vals) out.push(BigInt.asUintN(b, v).toString() + "/" + BigInt.asIntN(b, v).toString());
  console.log(b, out.join(" "));
}
const t0 = performance.now();
const big = BigInt.asUintN(20000000, 5n);
console.log(big.toString(), BigInt.asIntN(20000000, -5n).toString());
let h = 14695981039346656037n;
const P = 1099511628211n;
for (let i = 0; i < 1000000; i++) h = BigInt.asUintN(64, (h + BigInt(i & 255)) * P);
console.log(h.toString(), performance.now() - t0 < 2000);
try { BigInt.asUintN(-1, 1n); } catch (e) { console.log("throws", (e as Error).message); }
