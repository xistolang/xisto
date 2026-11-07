// Bytes: Uint8Array (indexing with 8-bit wrap, slice copies, subarray views, set, fill, indexOf, for...of),
// Buffer (from/toString with utf8, base64, base64url, hex, latin1; alloc, concat, equals), TextEncoder and
// TextDecoder (U+FFFD for bad UTF-8), btoa/atob, and ES2025 toBase64/fromBase64/toHex/fromHex.
const b = new Uint8Array(4);
b[0] = 1;
b[1] = 255;
b[2] = 256 + 7;
b[3] = -1;
console.log(b, b.length, b[0] + b[1], b[2], b[3]);
b[0] += 254;
b[1]++;
console.log(b[0], b[1], b);

const nums = new Uint8Array([10, 20, 30, 40, 50]);
const copy = nums.slice(1, -1);
const view = nums.subarray(1, 3);
view[0] = 99;
console.log(copy, view, nums, copy.length, view.length);
nums.set([7, 8], 3);
nums.set(new Uint8Array([1]), 0);
console.log(nums, nums.indexOf(99), nums.indexOf(5), nums.includes(8), nums.join("-"), nums.toString());
console.log(new Uint8Array(6).fill(9, 2, 4), Uint8Array.of(3, 2, 1).reverse(), Uint8Array.from([65, 66]));
let total: int = 0;
for (const x of nums) {
  total += x;
}
console.log(total);

const enc = new TextEncoder();
const bytes = enc.encode("ação ✓ 日本");
console.log(bytes.length, bytes.slice(0, 6), new TextDecoder().decode(bytes));
console.log(new TextDecoder().decode(new Uint8Array([104, 105, 0xff, 0xe2, 0x9c, 104])));

const buf = Buffer.from("hello, world ✓");
console.log(buf, buf.length, buf.toString(), buf.toString("hex"), buf.toString("base64"), buf.toString("base64url"));
console.log(Buffer.from("aGVsbG8gd29ybGQ=", "base64").toString(), Buffer.from("aGVsbG8", "base64").toString(), Buffer.from("68656c6c6fzz", "hex").toString());
console.log(Buffer.from("café", "latin1"), Buffer.from([0xe9]).toString("latin1"), buf.toString("utf8", 0, 5));
const joined = Buffer.concat([Buffer.from("ab"), Buffer.from("cd"), Buffer.alloc(2, 33)]);
console.log(joined, joined.toString(), joined.equals(Buffer.from("abcd!!")), Buffer.byteLength("ação"));
const sl = joined.slice(1, 3);
sl[0] = 90;
console.log(joined.toString(), sl.toString());

console.log(btoa("hello"), atob("aGVsbG8="), btoa("ÿ"));
const data = new Uint8Array([0, 1, 2, 250, 251, 252, 253, 254, 255]);
console.log(data.toBase64(), data.toHex(), Uint8Array.fromBase64(data.toBase64()), Uint8Array.fromHex("deadbeef"));
