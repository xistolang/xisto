// String.prototype.normalize in the four forms, as Node (ICU) gives them: every code point alone (a hash per block of
// 4096), random sequences of starters, combining marks of several classes, Hangul jamo and compatibility characters,
// and a few spelled out
function hash(s: string, h0: number): number {
  let h = h0;
  for (const ch of s) h = (h * 31 + (ch.codePointAt(0) ?? 0)) % 1000000007;
  return (h * 31 + 7) % 1000000007;
}
const forms: string[] = ["NFC", "NFD", "NFKC", "NFKD"];
for (const f of forms) {
  let line = "";
  for (let blk = 0; blk < 0x110; blk++) {
    let h = 0;
    for (let c = blk * 0x1000; c < blk * 0x1000 + 0x1000; c++) {
      if (c >= 0xd800 && c <= 0xdfff) continue;
      h = hash(String.fromCodePoint(c).normalize(f), h);
    }
    line += `${h} `;
  }
  console.log(f, line);
}
// sequences: starters, marks of several classes, jamo, compatibility characters
const pool: number[] = [0x41, 0x61, 0x45, 0x65, 0x4f, 0x6f, 0x55, 0x3b1, 0x3c9, 0x415, 0x1100, 0x1161, 0x11a8, 0xac00, 0xac01, 0x300, 0x301, 0x302, 0x303, 0x308, 0x323, 0x327, 0x328, 0x31b, 0x345, 0x5b0, 0x5b4, 0x5bc, 0x5c1, 0x93c, 0x94d, 0xe48, 0xfb01, 0x2460, 0x1e9b, 0x212b, 0x2126, 0x1d15e, 0x1d165, 0x1d16e, 0xf71, 0xf72, 0xf74, 0xf80, 0x344, 0x340, 0x341, 0x343, 0x374, 0x37e, 0x387, 0xcc6, 0xcc2, 0xcd5, 0x9c7, 0x9be, 0x9d7, 0x1b05, 0x1b35, 0x11131, 0x11127, 0x110a5, 0x110ba, 0xff76, 0xff9e, 0x3099, 0x304b, 0x309a, 0x3000, 0xa0, 0x2002, 0x1e0a, 0x1e0c, 0x307, 0x20, 0x16ff0, 0x611, 0x1df8, 0x1e2ae];
let seed = 12345;
function rnd(n: number): number {
  seed = (seed * 1103515245 + 12345) % 2147483648;
  return Math.floor((seed / 2147483648) * n);
}
for (const f of forms) {
  let h = 0;
  for (let k = 0; k < 20000; k++) {
    let s = "";
    const len = 1 + rnd(7);
    for (let j = 0; j < len; j++) s += String.fromCodePoint(pool[Math.trunc(rnd(pool.length))]);
    h = hash(s.normalize(f), h);
  }
  console.log(f, "seq", h);
}
const samples: string[] = ["é", "각", "각", "ﬁ", "①", "Å", "Å", "ẛ̣", "q̣̇", "̈́", "ཱི"];
for (const s of samples) {
  const parts: string[] = [];
  for (const f of forms) {
    let cs = "";
    for (const ch of s.normalize(f)) cs += Math.trunc(ch.codePointAt(0) ?? 0).toString(16) + " ";
    parts.push(`${f}: ${cs.trim()}`);
  }
  console.log(parts.join(" | "));
}
const word = "Amélie";
console.log(word.normalize(), word.normalize().length, word.normalize("NFD").length, word.normalize() === "Amélie");
console.log("ascii".normalize("NFKD"), "".normalize("NFD").length);
try {
  console.log("x".normalize("nfc"));
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
