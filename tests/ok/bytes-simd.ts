// base64 and hex through the vector paths (arm64: 48/64/16-byte blocks) and their scalar tails: every length
// around the block sizes, an invalid character at every position of a block, base64url and whitespace
let seed: int = 12345;
function next(): int {
  seed = (seed * 75 + 74) % 65537;
  return seed;
}
let report: string[] = [];
for (let n: int = 0; n <= 200; n++) {
  const b = new Uint8Array(n);
  for (let i: int = 0; i < n; i++) {
    b[i] = next() & 255;
  }
  const s64 = b.toBase64();
  const hex = b.toHex();
  const back = Uint8Array.fromBase64(s64);
  const back2 = Uint8Array.fromHex(hex);
  const nb = Buffer.from(s64, "base64");
  const nh = Buffer.from(hex, "hex");
  let ok: boolean = back.length === n && back2.length === n && nb.length === n && nh.length === n;
  for (let i: int = 0; i < n; i++) {
    if (back[i] !== b[i] || back2[i] !== b[i] || nb[i] !== b[i] || nh[i] !== b[i]) {
      ok = false;
    }
  }
  if (!ok || n % 50 === 0) {
    report.push(`${n}:${ok}:${s64.length}:${hex.slice(0, 8)}`);
  }
}
console.log(report.join(" "));
const base = new Uint8Array(96);
for (let i: int = 0; i < 96; i++) {
  base[i] = (i * 37 + 11) & 255;
}
const good64 = base.toBase64();
const goodHex = Buffer.from(base).toString("hex");
let lens: int[] = [];
for (let p: int = 0; p < 70; p++) {
  const bad64 = good64.slice(0, p) + "!" + good64.slice(p + 1);
  const badHex = goodHex.slice(0, p) + "z" + goodHex.slice(p + 1);
  lens.push(Buffer.from(bad64, "base64").length * 1000 + Buffer.from(badHex, "hex").length);
}
console.log(lens.join(","));
const url = Buffer.from(base).toString("base64url");
console.log(url.length, Buffer.from(url, "base64url").toString("hex").slice(0, 32));
const spaced = good64.slice(0, 40) + " \n" + good64.slice(40);
console.log(Buffer.from(spaced, "base64").toString("hex") === goodHex);
