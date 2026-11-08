// node:fs on file descriptors: openSync/closeSync, fstatSync (size, isFile), readSync into a Uint8Array (chunks of
// several sizes, explicit and current positions, a request past the end, a view on a SharedArrayBuffer)
import { openSync, closeSync, fstatSync, readSync, writeFileSync, unlinkSync } from "node:fs";

const path = "/tmp/xisto-fs-readsync.txt";
let text = "";
for (let i = 0; i < 300; i++) text += `line ${i} of the test file\n`;
writeFileSync(path, text);
const total = new TextEncoder().encode(text).length;

const fd = openSync(path, "r");
const st = fstatSync(fd);
console.log(st.size === total, st.isFile(), st.isDirectory());

// whole file in chunks of varied sizes at the current position
const out = new Uint8Array(st.size);
const sizes: int[] = [1, 7, 64, 1000, 4096, 100000];
let at = 0, k = 0, reads = 0;
while (at < out.length) {
  const want = Math.min(sizes[k % sizes.length], out.length - at);
  const got = readSync(fd, out, at, want, null);
  at += got;
  k++;
  reads++;
}
console.log(at === total, reads, new TextDecoder().decode(out) === text);
// at the end: nothing more
const tail = new Uint8Array(16);
console.log(readSync(fd, tail, 0, 16, null));

// explicit positions do not move the current one; a request larger than the rest is cut
const part = new Uint8Array(32);
const n1 = readSync(fd, part, 0, 32, 5);
console.log(n1, new TextDecoder().decode(part.subarray(0, n1)));
const n2 = readSync(fd, part, 0, 32, total - 10);
console.log(n2, JSON.stringify(new TextDecoder().decode(part.subarray(0, n2))));
console.log(readSync(fd, part, 0, 32, total + 100));
console.log(readSync(fd, tail, 0, 16, null)); // still at the end

// defaults: offset 0 and the buffer's length; offset only
const d = new Uint8Array(12);
console.log(readSync(fd, d, 0, 12, 0), new TextDecoder().decode(d));
const e = new Uint8Array(8);
e.fill(46);
console.log(readSync(fd, e, 4, 4, 0), new TextDecoder().decode(e));

// straight into shared memory (a view on a SharedArrayBuffer), at an offset
const sab = new SharedArrayBuffer(64);
const v = new Uint8Array(sab);
const n3 = readSync(fd, v, 8, 20, 0);
console.log(n3, new TextDecoder().decode(v.subarray(8, 8 + n3)), v[0], v[40]);
closeSync(fd);
unlinkSync(path);
