// Uint8Array.lastIndexOf and indexOf against every start, over arrays where the byte is rare, common and absent
// (the 16- and 8-byte steps of the search and the bytes left at the start)
let seed = 7;
function next(): int {
  seed = (seed * 1103 + 12345) % 65536;
  return seed >> 8;
}
let sum: int = 0, count: int = 0;
for (let n = 0; n <= 70; n += 3) {
  for (const spread of [2, 9, 40, 1000] as int[]) {
    const a = new Uint8Array(n);
    for (let i = 0; i < n; i++) a[i] = next() % spread === 0 ? 10 : 65 + next() % 4;
    for (let from = -n - 2; from <= n + 2; from++) {
      const l = a.lastIndexOf(10, from), f = a.indexOf(10, from);
      sum = (sum * 31 + l + 2 * f + 7) % 1000003;
      count++;
    }
    sum = (sum * 31 + a.lastIndexOf(10) + a.lastIndexOf(66) + a.lastIndexOf(300) + 9) % 1000003;
  }
}
console.log(count, sum);
const big = new Uint8Array(100);
big[0] = 10;
big[37] = 10;
console.log(big.lastIndexOf(10), big.lastIndexOf(10, 36), big.lastIndexOf(10, 37), big.lastIndexOf(0, 0), big.lastIndexOf(10, -64));
