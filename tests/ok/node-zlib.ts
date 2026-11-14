// node:zlib: deflate/inflate, gzip/gunzip, deflateRaw/inflateRaw, unzip (sync and callback forms), crc32,
// constants, the options (level, windowBits, memLevel, strategy, dictionary, finishFlush, maxOutputLength) and
// Node's errors. Compressed bytes are checked by round trip and by decompressing data Node made; their exact
// bytes are in node-zlib-bytes.ts.
import * as zlib from "node:zlib";
import { constants, crc32, gunzipSync, gzipSync, inflateSync } from "zlib";

function sample(n: int): Buffer {
  // deterministic text with repeats, so it compresses
  const words = ["alpha", "beta", "gamma", "delta", "épsilon", "日本", "zeta", "\n"];
  const parts: string[] = [];
  let x: int = 12345;
  let len = 0;
  while (len < n) {
    x = (x * 48271) % 2147483647;
    const w = words[x % words.length];
    parts.push(w);
    len += w.length + 1;
  }
  return Buffer.from(parts.join(" "));
}

const inputs: Buffer[] = [Buffer.alloc(0), Buffer.from("a"), Buffer.from("hello hello hello hello"), sample(5000), sample(300000)];
for (const input of inputs) {
  const d = zlib.deflateSync(input);
  const g = zlib.gzipSync(input);
  const r = zlib.deflateRawSync(input);
  console.log(
    input.length,
    zlib.inflateSync(d).equals(input),
    zlib.gunzipSync(g).equals(input),
    zlib.inflateRawSync(r).equals(input),
    zlib.unzipSync(d).equals(input),
    zlib.unzipSync(g).equals(input),
    d[0],
    d[1],
    g[0],
    g[1],
    g[2]
  );
}

// options: every level and strategy, windowBits, memLevel: round trips
const big = sample(20000);
for (let level = -1; level <= 9; level++) {
  const c = zlib.deflateSync(big, { level: level });
  console.log("level", level, inflateSync(c).equals(big), c.length < big.length || level === 0);
}
for (let strategy = 0; strategy <= 4; strategy++) {
  const c = zlib.gzipSync(big, { strategy: strategy });
  console.log("strategy", strategy, gunzipSync(c).equals(big));
}
const windows: int[] = [9, 10, 12, 15];
for (const wb of windows) {
  const c = zlib.deflateSync(big, { windowBits: wb, memLevel: 9 });
  console.log("windowBits", wb, zlib.inflateSync(c, { windowBits: wb }).equals(big), c[0].toString(16));
}
const raw8 = zlib.deflateRawSync(big, { windowBits: 8 });
console.log("raw windowBits 8", zlib.inflateRawSync(raw8).equals(big));

// data Node compressed (levels 1 and 9, gzip, raw)
const fromNode = [
  "eAHLSM3JyVdIK8rPVcjLT0nVUchJLUvNUcjPSwUAgSwJdQ==",
  "eNrLSM3JyVdIK8rPVcjLT0lVyKCQDwCjCB0H",
  "H4sIAAAAAAAAE0uvyixQSCvKz1XIy09JBQAgF+bRDgAAAA=="
];
for (const b of fromNode) console.log(zlib.unzipSync(Buffer.from(b, "base64")).toString());
console.log(zlib.inflateRawSync(Buffer.from("K0osV0grys9VyMtPSVUoQuYBAA==", "base64")).toString());

// gunzip reads members one after the other, and ignores trailing zero bytes
const two = Buffer.concat([gzipSync("first "), gzipSync("second"), Buffer.alloc(4)]);
console.log(gunzipSync(two).toString());

// dictionary
const dict = Buffer.from("hello world dictionary");
const withDict = zlib.deflateSync("hello world hello world", { dictionary: dict });
console.log(zlib.inflateSync(withDict, { dictionary: dict }).toString());
const rawDict = zlib.deflateRawSync("hello world hello world", { dictionary: dict });
console.log(zlib.inflateRawSync(rawDict, { dictionary: dict }).toString());

// a truncated stream: an error with Z_FINISH, what there is with Z_SYNC_FLUSH
const cut = zlib.deflateSync(big).subarray(0, 200);
const part = zlib.inflateSync(cut, { finishFlush: constants.Z_SYNC_FLUSH });
console.log(part.length > 0, big.subarray(0, part.length).equals(part));

function tryIt(label: string, f: () => void): void {
  try {
    f();
    console.log(label, "ok");
  } catch (e: any) {
    console.log(label, e.name, e.code, e.errno ?? "-", e.message);
  }
}
tryIt("not zlib", () => inflateSync(Buffer.from("hello")));
tryIt("empty", () => inflateSync(Buffer.alloc(0)));
tryIt("truncated", () => gunzipSync(gzipSync("abc").subarray(0, 10)));
tryIt("raw garbage", () => zlib.inflateRawSync(Buffer.from([0xff, 0xff])));
tryIt("no dictionary", () => inflateSync(withDict));
tryIt("level", () => zlib.deflateSync("x", { level: 10 }));
tryIt("windowBits", () => zlib.deflateSync("x", { windowBits: 7 }));
tryIt("memLevel", () => zlib.deflateSync("x", { memLevel: 0 }));
tryIt("strategy", () => zlib.deflateSync("x", { strategy: 9 }));
tryIt("maxOutputLength", () => inflateSync(zlib.deflateSync("abcabc".repeat(100)), { maxOutputLength: 10 }));
tryIt("maxOutputLength fits", () => inflateSync(zlib.deflateSync("abc"), { maxOutputLength: 10 }));

// crc32
console.log(crc32(""), crc32("hello"), crc32("world", crc32("hello ")), crc32(Buffer.from([0, 255, 1])), crc32(big), crc32("ação"));

// constants
console.log(constants.Z_NO_FLUSH, constants.Z_SYNC_FLUSH, constants.Z_FINISH, constants.Z_DATA_ERROR, constants.Z_BUF_ERROR);
console.log(constants.Z_BEST_SPEED, constants.Z_BEST_COMPRESSION, constants.Z_DEFAULT_COMPRESSION, constants.Z_RLE, constants.GUNZIP);
console.log(zlib.constants.Z_DEFAULT_WINDOWBITS, zlib.constants.Z_DEFAULT_MEMLEVEL, zlib.constants.Z_DEFAULT_CHUNK);

// the callback forms, one after the other
zlib.gzip("callback data", (err, gz) => {
  console.log("gzip", err, gz.length > 10);
  zlib.gunzip(gz, (err2, back) => {
    console.log("gunzip", err2, back.toString());
    zlib.deflate("with options", { level: 1 }, (err3, d) => {
      console.log("deflate", err3, zlib.inflateSync(d).toString());
      zlib.inflate(Buffer.from("not zlib"), (err4, r) => {
        console.log("inflate error", err4 !== null ? err4.message : "none");
        zlib.deflateRaw("raw", (e5, r5) => {
          zlib.inflateRaw(r5, (e6, r6) => {
            zlib.unzip(zlib.deflateSync("unzip"), (e7, r7) => console.log(e5, e6, e7, r6.toString(), r7.toString()));
          });
        });
      });
    });
  });
});
