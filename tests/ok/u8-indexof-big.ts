// Uint8Array indexOf over long arrays (xisto: 64 bytes per step on arm64 Linux, xs_memchr): the found byte at
// every distance from the start, in every lane of a 64-byte step and in the tail, from offsets, and absent
const n = 1000;
const a = new Uint8Array(n);
for (let i = 0; i < n; i++) a[i] = 65 + (i % 7); // letters A..G, never '>'
let sum: int = 0;
for (let at = 0; at < n; at += 3) {
  a[at] = 62; // one '>'
  sum += a.indexOf(62) * 7 + a.indexOf(62, 1) + a.indexOf(62, at) + a.indexOf(62, at + 1);
  a[at] = 65 + (at % 7);
}
console.log(sum, a.indexOf(62), a.indexOf(62, -100), a.indexOf(66, 990), a.indexOf(71, 64), a.includes(62));
const b = new Uint8Array(200);
b[63] = 9; b[64] = 9; b[127] = 9; b[199] = 9;
console.log(b.indexOf(9), b.indexOf(9, 64), b.indexOf(9, 65), b.indexOf(9, 128), b.indexOf(9, 200), b.indexOf(9, -1));
