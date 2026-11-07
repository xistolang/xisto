// Bitwise operators with JS semantics: 32-bit two's complement, shifts by count & 31, >>> unsigned, ~,
// compound forms, precedence (& above ^ above |, all below ===), ToInt32 of numbers, and nested generics.
const a: int = 0b1100;
const b: int = 0b1010;
console.log(a & b, a | b, a ^ b, ~a, a << 2, a >> 1, -a >> 1, -a >>> 28, 1 << 31, 1 << 32, 1 << 33);
console.log(0xffffffff | 0, 4294967296 | 0, 2147483648 | 0, -1 >>> 0, (-1 >>> 0) + 1, 3.9 | 0, -3.9 | 0);
let x: int = 0xff;
x &= 0x0f;
x |= 0x30;
x ^= 0x01;
x <<= 4;
x >>= 2;
let y: int = -16;
y >>>= 28;
console.log(x, y);
console.log(1 | 2 & 3, (1 | 2) & 3, (5 & 3) === 1, 1 + 2 << 1, 8 >> 1 + 1);
const n: number = 1e10;
console.log(n | 0, n >>> 0, ~~n, ~~(-7.5), 2 ** 3);
// hashing: FNV-1a 32-bit over a string's bytes
const bytes = new TextEncoder().encode("hello world");
let h: int = 0x811c9dc5;
for (const c of bytes) {
  h ^= c;
  h = Math.imul(h, 0x01000193);
}
console.log(h >>> 0, (h >>> 0).toString(16));
// generics still close with >>
const m: Map<string, Array<int>> = new Map<string, Array<int>>();
m.set("k", [1, 2]);
const arr: Array<Array<int>> = [[1], [2, 3]];
console.log(m.get("k"), arr.length, arr[1][1] >> 1);
const buf = new Uint8Array([1, 2, 3]);
buf[0] |= 0xf0;
buf[1] <<= 7;
console.log(buf[0], buf[1]);
