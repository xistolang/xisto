// node:string_decoder: StringDecoder over chunks that split characters (same output as Node)
import { StringDecoder } from "node:string_decoder";

function bytes(...xs: int[]): Buffer {
  return Buffer.from(xs);
}

// feeds the bytes in chunks of the given sizes and prints each piece of text
function feed(encoding: string, data: Buffer, sizes: int[]): void {
  const d = new StringDecoder(encoding);
  const parts: string[] = [];
  let at: int = 0;
  for (const n of sizes) {
    parts.push(d.write(data.subarray(at, at + n)));
    at += n;
  }
  parts.push(d.end(data.subarray(at)));
  console.log(d.encoding, JSON.stringify(parts));
}

// ---- utf8 ----
const text = Buffer.from("aé€😀z", "utf8");
for (let i = 0; i <= text.length; i++) feed("utf8", text, [i]);
feed("utf8", text, [1, 1, 1, 1, 1, 1, 1, 1, 1, 1]);
feed("utf-8", text, [2, 3, 4]);
feed("utf8", bytes(0xe2, 0x82), []);
feed("utf8", bytes(0xe2, 0x82, 0x41), [2]);
feed("utf8", bytes(0xe2, 0x41, 0x42), [1]);
feed("utf8", bytes(0xf0, 0x9f, 0x98), [1, 1]);
feed("utf8", bytes(0x80, 0x80, 0x41), [1]);
feed("utf8", bytes(0xc3, 0xa9, 0xff, 0xfe, 0x41), [1, 2]);
feed("utf8", bytes(0xed, 0xa0, 0x80, 0x41), [2]);
feed("utf8", bytes(0xc0, 0xaf, 0xe0, 0x80, 0x80), [1, 2]);
feed("utf8", bytes(0xf8, 0x88, 0x80, 0x80, 0x80), [3]);
feed("utf8", bytes(0x61, 0x80, 0x80, 0x80, 0x80), [5]);
feed("utf8", Buffer.from("plain ascii text"), [5, 5]);

const lines = new StringDecoder("utf8");
let out = "";
const big = Buffer.from("Olá, mundo! Ação, coração, 😀 emoji, 中文字符\n".repeat(20));
for (let i = 0; i < big.length; i += 7) out += lines.write(big.subarray(i, i + 7 < big.length ? i + 7 : big.length));
out += lines.end();
console.log(out === big.toString(), Buffer.byteLength(out));

// ---- utf16le ----
const u16 = bytes(0x41, 0x00, 0xe9, 0x00, 0x3d, 0xd8, 0x00, 0xde, 0x5a, 0x00);
// (a chunk that ends inside a surrogate pair at an odd byte gives Node lone surrogates: left out, see the docs)
for (let i = 0; i <= u16.length; i++) if (i !== 7) feed("utf16le", u16, [i]);
feed("ucs2", bytes(0x41, 0x00, 0xe9, 0x00, 0xac, 0x20, 0x5a, 0x00), [1, 1, 1, 1, 1, 1, 1, 1]);
feed("utf16le", u16, [4, 2, 2]);
feed("utf-16le", u16, [3, 3, 3]);
feed("utf16le", bytes(0x41, 0x00, 0x42), []);

// ---- base64, base64url, hex, latin1, ascii ----
const bin = bytes(0xfb, 0xff, 0x00, 0x41, 0x42, 0x43, 0x7f, 0x80, 0xe9, 0xff);
for (const enc of ["base64", "base64url", "hex", "latin1", "binary", "ascii"]) {
  feed(enc, bin, [1, 2, 4]);
  feed(enc, bin, [5]);
}

// ---- end() resets, write of an empty buffer, unknown encoding ----
const d = new StringDecoder();
console.log(JSON.stringify(d.write(bytes(0xe2))), JSON.stringify(d.end()), JSON.stringify(d.write(bytes(0x41))), JSON.stringify(d.end()));
console.log(JSON.stringify(d.write(Buffer.alloc(0))), JSON.stringify(d.end(Buffer.from("x"))));
console.log(JSON.stringify(new StringDecoder("UTF8").write(new Uint8Array([0x68, 0x69]))));
try {
  new StringDecoder("klingon");
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
