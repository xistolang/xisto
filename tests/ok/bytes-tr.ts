// Loops over bytes the compiler turns into one runtime call (dst[t + e] = table[src[f - t]], copies too),
// at lengths around the 16/32-byte blocks and on every kind of overlap, plus reverse() and copyWithin():
// the result must be the sequential loop's, as Node runs it
let seed: int = 7;
function next(): int {
  seed = (seed * 75 + 74) % 65537;
  return seed;
}
function sum(b: Uint8Array): int {
  let h: int = 0;
  for (let i = 0; i < b.length; i++) h = (h * 31 + b[i]) % 1000000007;
  return h;
}
const table = new Uint8Array(256);
for (let i = 0; i < 256; i++) table[i] = i;
for (let i = 65; i < 91; i++) { table[i] = 155 - i; table[i + 32] = 155 - i; } // letters mirrored (four blocks)
const scrambled = new Uint8Array(256);
for (let i = 0; i < 256; i++) scrambled[i] = (i * 167 + 13) & 255; // every block
const short = new Uint8Array(123); // a table shorter than 256: the loop as written (every lookup in range here)
for (let i = 0; i < 123; i++) short[i] = 122 - i;
const report: string[] = [];
const lens: int[] = [0, 1, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 255];
for (const len of lens) {
  const src = new Uint8Array(len + 300);
  for (let i = 0; i < src.length; i++) src[i] = next() & 255;
  const letters = new Uint8Array(len + 300);
  for (let i = 0; i < letters.length; i++) letters[i] = 65 + (next() % 58);
  const dst = new Uint8Array(len + 300);
  const n: int = len;
  const parts: int[] = [];
  // a copy, forward and backwards
  for (let t = 0; t < n; t++) dst[5 + t] = src[7 + t];
  parts.push(sum(dst));
  for (let t = 0; t < n; t++) dst[9 + t] = src[n + 20 - t];
  parts.push(sum(dst));
  // through a table, forward and backwards, with few blocks and with every block
  for (let t = 0; t < n; t++) dst[t] = table[letters[t + 3]];
  parts.push(sum(dst));
  for (let t = 0; t < n; t++) dst[t + 1] = table[letters[n + 50 - t]];
  parts.push(sum(dst));
  for (let t = 0; t < n; t++) dst[t] = scrambled[src[t]];
  parts.push(sum(dst));
  for (let t = 0; t < n; t++) dst[2 + t] = short[letters[t]];
  parts.push(sum(dst));
  // in place: the same bytes through the table; shifted (each step reads what the step before wrote);
  // reversed onto itself (the second half reads the first half's new values)
  const same = letters.slice();
  for (let t = 0; t < n; t++) same[t + 10] = table[same[t + 10]];
  parts.push(sum(same));
  for (let t = 0; t < n; t++) same[t + 1] = same[t];
  parts.push(sum(same));
  for (let t = 0; t < n; t++) same[t] = table[same[n - 1 - t]];
  parts.push(sum(same));
  for (let t = 0; t < n; t++) same[t + 1] = scrambled[same[t]];
  parts.push(sum(same));
  // the table inside the written range
  const tbl = new Uint8Array(400);
  for (let i = 0; i < 400; i++) tbl[i] = (i * 7) & 255;
  for (let t = 0; t < n; t++) tbl[t + 100] = tbl[src[t]];
  parts.push(sum(tbl));
  // a start other than 0
  const lo: int = 3;
  for (let t = lo; t < n; t++) dst[t - lo] = table[src[t - 1]];
  parts.push(sum(dst));
  // reverse() in place on a view, copyWithin() forward and backwards with the JS clamping
  const r = new Uint8Array(src);
  r.subarray(2, 2 + n).reverse();
  parts.push(sum(r));
  r.copyWithin(10, 3, 3 + n);
  parts.push(sum(r));
  r.copyWithin(0, 20);
  parts.push(sum(r));
  r.copyWithin(-n - 5, -n - 50, -n - 20);
  parts.push(sum(r));
  report.push(`${len}:${parts.join(",")}`);
}
console.log(report.join("\n"));
