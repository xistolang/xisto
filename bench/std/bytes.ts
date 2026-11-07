// Bytes under load: fill and checksum 64 MB through indexing, base64 and hex both ways over 16 MB, UTF-8
// encode/decode of a 8 MB text, and 100k small Buffers concatenated.
const big = new Uint8Array(64 * 1024 * 1024);
for (let i: int = 0; i < big.length; i++) {
  big[i] = (i * 31 + 7) & 255;
}
let sum: int = 0;
for (let i: int = 0; i < big.length - 13; i++) {
  sum = (sum + big[i] * ((i >> 3) & 15) + (i & 7)) & 0xfffffff;
}
console.log("checksum", sum);

const chunk = big.subarray(0, 16 * 1024 * 1024);
const b64 = chunk.toBase64();
const back = Uint8Array.fromBase64(b64);
const hex = Buffer.from(back).toString("hex");
const fromHex = Buffer.from(hex, "hex");
console.log("base64", b64.length, back.length, hex.length, fromHex.length, fromHex[12345]);

const parts: string[] = [];
for (let i: int = 0; i < 200000; i++) {
  parts.push("línea " + String(i) + " ✓ ");
}
const text = parts.join("");
const encoded = new TextEncoder().encode(text);
const decoded = new TextDecoder().decode(encoded);
console.log("utf8", encoded.length, decoded.length);

const pieces: Buffer[] = [];
for (let i: int = 0; i < 100000; i++) {
  pieces.push(Buffer.from("piece-" + String(i)));
}
const all = Buffer.concat(pieces);
console.log("concat", all.length, all.toString("utf8", 0, 7));
