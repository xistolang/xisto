// the two byte-array loops the emitter turns into one call: the symmetric swap (a reversal of a[lo..S-lo]) and
// the element copy, over ranges of every length, at the edges, in buffers of 16 bytes and of other sizes
function rev(a: Uint8Array, lo: int, S: int): void {
  for (let i = lo; i < (S + 1) >> 1; i++) {
    const t = a[i];
    a[i] = a[S - i];
    a[S - i] = t;
  }
}
function copy(dst: Uint8Array, src: Uint8Array, lo: int, hi: int): void {
  for (let j = lo; j < hi; j++) dst[j] = src[j];
}
function fill(a: Uint8Array): void {
  for (let i = 0; i < a.length; i++) a[i] = 100 + i;
}
function show(a: Uint8Array): string {
  let s = "";
  for (let i = 0; i < a.length; i++) s += (i > 0 ? "," : "") + String(a[i]);
  return s;
}
const a = new Uint8Array(16);
for (let S = 0; S <= 15; S++) {
  fill(a);
  rev(a, 0, S);
  console.log("rev 0..", S, show(a));
}
for (let lo = 0; lo <= 8; lo++) {
  fill(a);
  rev(a, lo, 15);
  console.log("rev", lo, "..", 15 - lo, show(a));
}
fill(a);
rev(a, 5, 4); // empty range: nothing happens
rev(a, 9, 15); // lo past the middle: nothing happens
console.log("empty", show(a));
const b = new Uint8Array(40);
fill(b);
rev(b, 3, 30);
rev(b, 0, 39);
console.log("big", show(b));
const c = new Uint8Array(7);
fill(c);
rev(c, 0, 6);
rev(c, 1, 4);
console.log("small", show(c));
const d = new Uint8Array(16);
copy(d, a, 0, 16);
console.log("copy all", show(d));
copy(d, b, 4, 9);
console.log("copy part", show(d));
copy(d, c, 2, 2);
console.log("copy none", show(d));
const e = new Uint8Array(5);
copy(e, c, 1, 5);
console.log("copy small", show(e));
// the same shapes on a view keep working (a view shares the bytes)
const v = b.subarray(8, 24);
rev(v, 0, 15);
console.log("view", show(v), show(b));
// a reversal followed by a store into the same bytes (one call): the store inside the reversed segment, at
// its end, past it, and on a buffer where the vector path does not apply
function revPut(a: Uint8Array, lo: int, S: int, e: int, v: int): void {
  for (let i = lo; i < (S + 1) >> 1; i++) {
    const t = a[i];
    a[i] = a[S - i];
    a[S - i] = t;
  }
  a[e] = v;
}
fill(a);
revPut(a, 1, 9, 9, 99);
console.log("revput end", show(a));
fill(a);
revPut(a, 1, 9, 4, 77);
console.log("revput inside", show(a));
fill(a);
revPut(a, 0, 15, 15, 1);
console.log("revput last", show(a));
fill(a);
revPut(a, 6, 11, 0, 5); // empty reversal, the store still happens
console.log("revput empty", show(a));
fill(c);
revPut(c, 0, 5, 6, 42);
console.log("revput small", show(c));
fill(b);
revPut(b, 2, 20, 21, 3);
console.log("revput big", show(b));
