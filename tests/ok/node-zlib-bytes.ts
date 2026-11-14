// node:zlib: the compressed bytes are Node's, byte for byte, for every level, strategy, windowBits and memLevel
// (zlib's deflate is deterministic: the same library gives the same stream). Each output is printed as its length
// and CRC-32. The gzip header's OS byte is the platform's (zlib's OS_CODE: 3 on Linux, 19 on macOS), as in Node.
import { crc32, deflateRawSync, deflateSync, gzipSync } from "node:zlib";

function sample(n: int, seed: int): Buffer {
  const words = ["lorem", "ipsum", "dolor", "sit", "amet", "consectetur", "ação", "日本語", "{\"k\": 1}", "\n", "0123456789"];
  const parts: string[] = [];
  let x: int = seed;
  let len = 0;
  while (len < n) {
    x = (x * 48271) % 2147483647;
    const w = words[x % words.length];
    parts.push(w);
    len += w.length + 1;
  }
  return Buffer.from(parts.join(x % 3 === 0 ? " " : ","));
}

function show(label: string, b: Buffer): void {
  console.log(label, b.length, crc32(b));
}

const inputs: Buffer[] = [Buffer.alloc(0), Buffer.from("hello"), sample(1000, 7), sample(70000, 99), sample(400000, 12345)];
const bytes = new Uint8Array(50000);
let s: int = 1;
for (let i = 0; i < bytes.length; i++) {
  s = (s * 48271) % 2147483647;
  bytes[i] = s % 256; // incompressible
}
inputs.push(Buffer.from(bytes));
for (let k = 0; k < inputs.length; k++) {
  const input = inputs[k];
  for (let level = -1; level <= 9; level++) show(`in${k} level ${level}`, deflateSync(input, { level: level }));
  for (let strategy = 1; strategy <= 4; strategy++) show(`in${k} strategy ${strategy}`, deflateSync(input, { strategy: strategy }));
  const windows: int[] = [9, 11, 13, 15];
  for (const wb of windows) show(`in${k} windowBits ${wb}`, deflateRawSync(input, { windowBits: wb }));
  for (let mem = 1; mem <= 9; mem += 4) show(`in${k} memLevel ${mem}`, deflateSync(input, { memLevel: mem, level: 9 }));
  show(`in${k} gzip`, gzipSync(input));
}
console.log(gzipSync("hi").subarray(0, 9).toString("hex"), deflateSync("hello").toString("hex"));
show("chunkSize 1024 level 0", deflateSync(inputs[3], { level: 0, chunkSize: 1024 }));
show("chunkSize 100000 level 0", gzipSync(inputs[4], { level: 0, chunkSize: 100000 }));
