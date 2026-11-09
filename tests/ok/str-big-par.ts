// a long string's length and its UTF-8 bytes counted and copied over the threads (16 MB and more): the same as one
// pass, with characters of two and three bytes spread through it and at the pieces' edges

function build(n: int): string {
  const parts: string[] = [];
  let x = 5;
  for (let i = 0; i < n; i++) {
    x = (x * 1103 + 12345) % 2147483648;
    const r = Math.trunc(x / 65536) % 64;
    parts.push(r === 0 ? "é" : r === 1 ? "€" : r === 2 ? "\n" : "acgtACGT".charAt(r % 8));
  }
  return parts.join("");
}

const s = build(17000000);
console.log(s.length, s.indexOf("€"), s.lastIndexOf("é"), s.indexOf("é", 16000000));
const bytes = new TextEncoder().encode(s);
let sum: int = 0;
for (let i = 0; i < bytes.length; i += 4099) sum = (sum + bytes[i] * (i % 97)) % 1000000007;
console.log(bytes.length, sum, bytes[bytes.length - 1]);
const ascii = s.replaceAll("é", "").replaceAll("€", "");
console.log(ascii.length, new TextEncoder().encode(ascii).length, ascii.indexOf("\n", 15000000));
// the first occurrence searched over the threads: at the start, across where the pieces meet, at the end, absent
const marks: int[] = [0, 1, 4194303, 4194304, 4194305, 8388607, 8388608, 12582910, 16777215, 16999990];
const pieces: string[] = [];
let last: int = 0;
for (const m of marks) {
  pieces.push(s.slice(last, m));
  pieces.push("<<XYZ>>");
  last = m;
}
pieces.push(s.slice(last));
const marked = pieces.join("");
let at = -1;
const found: int[] = [];
while ((at = marked.indexOf("<<XYZ>>", at + 1)) >= 0) found.push(at);
console.log(found.join(" "), marked.indexOf("<<XYZ>>!"), marked.indexOf("€", 16900000), marked.indexOf("XYZ>><<"), marked.lastIndexOf("<<XYZ>>"));
const mbytes = new TextEncoder().encode(marked);
const firsts: int[] = [];
const froms: int[] = [0, 5, 4194300, 8388610, 16999000];
for (const from of froms) firsts.push(mbytes.indexOf(60, from));
console.log(firsts.join(" "), mbytes.indexOf(255), mbytes.indexOf(62, mbytes.length - 3), mbytes.includes(90));
