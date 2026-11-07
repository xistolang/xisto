// bounds-check elimination on Uint8Array (emit.c): `for (...; i < b.length; i++)`, versioned loops over bytes
// (b[i], b[E + i], b[E - i]), and `const t = new Uint8Array(256)` read with a byte (its range is within the
// length). Out-of-bounds indices still fail where they failed. Also: && and || of plain comparisons, evaluated
// without the short circuit (flat_bool), give the same results.
const b = new Uint8Array(16);
for (let i = 0; i < b.length; i++) b[i] = i * 17;
let s: int = 0;
for (let i = 0; i < b.length; i++) s += b[i];
console.log("sum", s);

const table = new Uint8Array(256);
for (let i = 0; i < 256; i++) table[i] = 255 - i;
const n: int = 12;
for (let i = 0; i < n; i++) b[i] = table[b[i]]; // versioned: i < n <= b.length; table[byte]: no check
console.log(b.join(","));

// reverse-complement style: two cursors, from the front and from the back
const seq = new TextEncoder().encode("AACCGTGTTACG");
const comp = new Uint8Array(256);
comp[65] = 84; comp[84] = 65; comp[67] = 71; comp[71] = 67;
const a0: int = 0;
const b0: int = seq.length - 1;
const run: int = Math.trunc(seq.length / 2);
for (let t = 0; t < run; t++) {
  const x = seq[a0 + t];
  seq[a0 + t] = comp[seq[b0 - t]];
  seq[b0 - t] = comp[x];
}
console.log(new TextDecoder().decode(seq));
for (let t = 0; t < 4; t++) seq[t + 2] += 1; // compound assignment, versioned
console.log(seq.join(","));

try {
  for (let i = 0; i < 20; i++) s += b[i]; // past the end: the checked loop, and its error
} catch (e) {
  console.log("past the end caught:", e.message, s);
}
let back: int = 3;
try {
  for (let i = 0; i < 6; i++) s += b[back - i]; // reaches b[-1]
} catch (e) {
  console.log("below zero caught:", e.message, s);
}
const small = new Uint8Array(4);
let big: int = 300;
console.log(table[big - 50]); // not a byte: checked, and within
try {
  console.log(table[big - 40]); // not a byte: checked, and past the end
} catch (e) {
  console.log("not a byte caught:", e.message);
}
try {
  console.log(small[b[0]]); // a byte, but small has only 4
} catch (e) {
  console.log("small table caught:", e.message);
}

// && and || without the short circuit: NaN, mixed int and number, nested
const xs: number[] = [0.5, NaN, 3, -1];
let c: int = 0;
for (let i = 0; i < xs.length; i++) {
  const x = xs[i];
  const k: int = i;
  if (!(x <= 1) && !(x > 4) || k === 3) c += 10;
  if (x > 0 && k < 2 && !(x !== x)) c += 1;
}
console.log("flat", c);
