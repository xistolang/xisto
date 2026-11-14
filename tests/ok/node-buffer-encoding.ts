// node:buffer: every encoding both ways, and what Node makes of input that is not valid (hex, base64, UTF-8 with
// U+FFFD for each maximal subpart, UTF-16 with an odd byte)

// encodings, both directions, and what Node does with input that is not valid
const text = "héllo wörld € 😀";
const encs = ["utf8", "utf-8", "hex", "base64", "base64url", "latin1", "binary", "ascii", "ucs2", "ucs-2", "utf16le", "utf-16le", "UTF8", "Hex"];
for (const enc of encs) {
  const b = Buffer.from(text, enc as BufferEncoding);
  console.log(enc, b.length, b.toString(enc as BufferEncoding));
}
const bytes = Buffer.from([0x00, 0x41, 0x7f, 0x80, 0xc3, 0xa9, 0xff, 0xfe, 0xfb, 0xef]);
for (const enc of encs) console.log(enc, JSON.stringify(bytes.toString(enc as BufferEncoding)));
console.log(Buffer.from("héllo", "utf8").toString("hex"), Buffer.from("héllo").toString("base64"), Buffer.from("héllo").toString("base64url"));
console.log(bytes.toString("hex", 2, 5), bytes.toString("utf8", 4, 6), bytes.toString("latin1", -3, 2), bytes.toString("base64", 1), bytes.toString("ascii", 3, 100));

// hex: pairs until the first one that is not hex; an odd last digit is dropped
for (const h of ["", "a", "ab", "abc", "0g12", "12g4", "ABcdEF", "  12", "12 34", "1x"]) console.log(JSON.stringify(h), Buffer.from(h, "hex"));
// base64: whitespace and characters outside the alphabet skipped, - and _ accepted, padding optional
for (const s of ["aGk=", "aGk", "aG k=", "aGk=\n", "a\nG\tk", "a-_b", "a+/b", "aGk=aGk=", "a===", "=aGk", "!!aGk", "aGVsbG8gd29ybGQ", "YQ", "Y", "YW=x"]) {
  console.log(JSON.stringify(s), Buffer.from(s, "base64"), Buffer.from(s, "base64url"));
}
// utf8: each ill-formed sequence becomes U+FFFD, the maximal subpart once
const bad: int[][] = [[0xc3], [0xc3, 0x28], [0xe2, 0x82], [0xe2, 0x82, 0x28], [0xe0, 0x80, 0x80], [0xed, 0xa0, 0x80], [0xf4, 0x90, 0x80, 0x80],
  [0xf0, 0x9f, 0x98], [0xf0, 0x9f, 0x98, 0x41], [0xc0, 0x80], [0xf8, 0x88, 0x80, 0x80, 0x80], [0x80, 0x80], [0xef, 0xbb, 0xbf, 0x41], [0xf0, 0x9f, 0x98, 0x80]];
for (const b of bad) {
  const s = Buffer.from(b).toString();
  console.log(Buffer.from(b), JSON.stringify(s), Buffer.from(s));
}
// ucs2: whole units; an odd last byte dropped; surrogate pairs
console.log(Buffer.from("a😀b", "ucs2"), Buffer.from([0x61, 0x00, 0x62]).toString("ucs2"), Buffer.from([0x3d, 0xd8, 0x00, 0xde]).toString("utf16le"));
// latin1 and ascii writing keeps the low byte of each UTF-16 unit
console.log(Buffer.from("é€😀", "latin1"), Buffer.from("é€😀", "ascii"), Buffer.from([0xe9, 0x80]).toString("latin1"), Buffer.from([0xe9, 0x80]).toString("ascii"));
for (const e of ["utf8", "UTF-8", "Utf16le", "ucs2", "binary", "base64url", "nope", ""]) console.log(e, Buffer.isEncoding(e));
for (const [s, e] of [["héllo", "utf8"], ["aGVsbG8=", "base64"], ["aGVsbG8", "base64url"], ["abcd", "hex"], ["abc", "hex"], ["h😀", "ucs2"], ["h😀", "latin1"], ["x", "nope"], ["", "base64"], ["=", "base64"]]) {
  console.log(s, e, Buffer.byteLength(s, e as BufferEncoding));
}
try {
  Buffer.from("x", "nope" as BufferEncoding);
} catch (e: any) {
  console.log(e.name, e.code, e.message);
}
try {
  Buffer.from("x").toString("nope" as BufferEncoding);
} catch (e: any) {
  console.log(e.name, e.code, e.message);
}
