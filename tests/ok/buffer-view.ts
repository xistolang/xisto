// Buffer.from(view.buffer, view.byteOffset, view.byteLength): a Buffer on the same memory, not a copy; an offset and
// a length are checked against that memory.
const b = Buffer.from("hello world");
const v = b.subarray(6, 11);
const w = Buffer.from(v.buffer, v.byteOffset, v.byteLength);
console.log(w.toString("base64"), w.toString(), w.length);
w[0] = 87;
console.log(b.toString());
const u = new Uint8Array([1, 2, 3, 4]);
const x = Buffer.from(u.buffer, 1, 2);
console.log(x, Buffer.isBuffer(x));
x[1] = 9;
console.log(u);
const y = Buffer.from(u.buffer, 2);
console.log(y.length, y[0]);
const bad: [int, int][] = [[1, 9], [7, 0]];
for (const [off, len] of bad) {
  try {
    Buffer.from(u.buffer, off, len);
  } catch (e) {
    console.log((e as Error).message);
  }
}
// a value of unknown type, narrowed by Buffer.isBuffer / ArrayBuffer.isView: its bytes
function encode(v: unknown): string {
  if (Buffer.isBuffer(v) || ArrayBuffer.isView(v)) {
    return Buffer.from(v.buffer, v.byteOffset, v.byteLength).toString("base64");
  }
  return "not bytes";
}
console.log(encode(Buffer.from("abc")), encode(new Uint8Array([1, 2, 3]).subarray(1)), encode("x"));
