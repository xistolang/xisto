// Index access on strings that are not ASCII (XB-214): charAt / charCodeAt / codePointAt / substring / slice /
// indexOf walk from a per-thread cached position instead of from the start. Forward, backward and random access,
// several strings in turn (more than the cache holds), strings freed and made again at the same address inside
// the loop, a string grown in place while indexed. Characters: Latin-1 (2 bytes), BMP (3 bytes) and astral
// (4 bytes; their part only uses positions found by indexOf, where code points and UTF-16 units agree).

function hash(h: number, t: string): number {
  for (let k = 0; k < t.length; k++) h = (h * 31 + t.charCodeAt(k)) % 1000000007;
  return (h * 31 + t.length) % 1000000007;
}

function make(n: number, seed: number): string {
  const pieces = ["a", "é", "€", "bc", "ñ", "中", "ü", "z", "日本", "ç"];
  let s = "";
  let x = seed;
  for (let i = 0; i < n; i++) {
    x = (x * 1103515245 + 12345) % 2147483648;
    s += pieces[Math.trunc(x % pieces.length)];
  }
  return s;
}

// forward: s.substring(i, i + k), s.charAt(i), s.charCodeAt(i)
function forward(s: string): number {
  let h = 0;
  for (let i = 0; i < s.length; i++) {
    h = hash(h, s.substring(i, i + 3));
    h = (h + s.charCodeAt(i)) % 1000000007;
    h = hash(h, s.charAt(i));
  }
  return h;
}

// backward: s.slice(i - k, i) going down, negative slice indices
function backward(s: string): number {
  let h = 0;
  for (let i = s.length; i > 0; i--) {
    h = hash(h, s.slice(i - 4 < 0 ? 0 : i - 4, i));
    if (i % 5 === 0) h = hash(h, s.slice(-i, -i + 2));
    h = (h + (s.codePointAt(i - 1) ?? 0)) % 1000000007;
  }
  return h;
}

// random: jumps all over the string, and windows around each jump
function random(s: string, steps: number): number {
  let h = 0;
  let x = 7;
  for (let k = 0; k < steps; k++) {
    x = (x * 1103515245 + 12345) % 2147483648;
    const i = x % s.length;
    h = hash(h, s.substring(i, i + 5));
    h = hash(h, s.slice(i - 3 < 0 ? 0 : i - 3, i + 1));
    h = (h + s.charCodeAt(i) + s.charCodeAt(s.length - 1 - i)) % 1000000007;
  }
  return h;
}

// indexOf from a position, lastIndexOf: byte offsets back to indexes
function searches(s: string, needle: string): number {
  let h = 0;
  let p = s.indexOf(needle);
  let count = 0;
  while (p >= 0) {
    h = (h * 7 + p) % 1000000007;
    h = hash(h, s.slice(p, p + 3));
    count++;
    p = s.indexOf(needle, p + 1);
  }
  return (h + count * 13 + s.lastIndexOf(needle)) % 1000000007;
}

const texts: string[] = [];
for (let t = 0; t < 6; t++) texts.push(make(300 + t * 97, t + 1));
texts.push("a".repeat(5000) + "é");
texts.push("é" + "b".repeat(3000) + "€" + "c".repeat(3000));
for (let t = 0; t < texts.length; t++) {
  const s = texts[t];
  console.log(t, s.length, forward(s), backward(s), random(s, 2000), searches(s, "é"), searches(s, "中"));
}

// several strings in turn, more than the cache holds
{
  let h = 0;
  for (let i = 0; i < 2000; i++)
  for (let t = 0; t < texts.length; t++) {
    const s = texts[t];
    const j = (i * 7 + t * 13) % s.length;
    h = hash(h, s.substring(j, j + 2));
  }
  console.log("interleaved", h);
}

// a string freed and another of the same size made in its place, every iteration: the cache must not answer for
// the old one (2-byte characters moved around, the byte length the same; one allocation per string, so the block
// just freed is the one taken)
{
  const pre: string[] = [];
  const bigPre: string[] = [];
  for (let k = 0; k < 10; k++) {
    pre.push("a".repeat(2 * k) + "é".repeat(10 - k));
    bigPre.push("é".repeat(k * 30) + "a".repeat(600 - k * 60));
  }
  const tail = "x".repeat(60) + "€";
  let h = 0;
  for (let j = 0; j < 3000; j++) {
    const k = (j * 7) % 10;
    const t = pre[k] + tail;
    h = (h + t.charCodeAt(15) + t.charCodeAt(9) * 3 + t.charCodeAt(t.length - 1)) % 1000000007;
    h = hash(h, t.substring(8, 14));
    const big = bigPre[k] + tail;
    h = (h + big.charCodeAt(250) + big.charCodeAt(big.length - 70) * 5 + big.indexOf("x")) % 1000000007;
    h = hash(h, big.slice(250, 256));
  }
  console.log("reused", h);
}

// grown in place while indexed
{
  let s = "é";
  let h = 0;
  for (let i = 0; i < 3000; i++) {
    s += i % 3 === 0 ? "abc€" : "xñ";
    if (i % 7 === 0) h = hash(h, s.charAt(s.length - 2) + s.slice(-5, -2) + s.substring(i, i + 2));
  }
  console.log("grown", s.length, h);
}

// astral characters (4 bytes): only positions found by search, where both counts land on the same character, and
// the pieces hashed by code point (for...of)
function hashCp(h: number, t: string): number {
  for (const ch of t) h = (h * 31 + (ch.codePointAt(0) ?? 0)) % 1000000007;
  return (h * 31 + 1) % 1000000007;
}
{
  const parts: string[] = [];
  for (let i = 0; i < 400; i++) parts.push(i % 3 === 0 ? "😀" + "abcdef" : i % 3 === 1 ? "é𝄞中" + "ghij" : "xyz€🎉");
  const s = parts.join("-");
  let h = 0;
  let p = s.indexOf("😀");
  let n = 0;
  while (p >= 0) {
    let q = s.indexOf("-", p);
    if (q < 0) q = s.length;
    h = hashCp(h, s.slice(p, q));
    h = (h + (s.codePointAt(p) ?? 0)) % 1000000007;
    h = hashCp(h, s.substring(q, q + 2));
    n++;
    p = s.indexOf("😀", q);
  }
  let back = 0;
  let r = s.lastIndexOf("🎉");
  while (r > 0) {
    back++;
    h = (h + (s.codePointAt(r) ?? 0)) % 1000000007;
    r = s.slice(0, r).lastIndexOf("🎉"); /* (lastIndexOf takes no position in xisto) */
  }
  const mid = s.indexOf("é𝄞中", s.indexOf("-xyz"));
  console.log("astral", n, back, h, s.slice(mid, s.indexOf("-", mid)));
}
