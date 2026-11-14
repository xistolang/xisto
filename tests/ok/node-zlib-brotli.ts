// node:zlib's Brotli: brotliCompressSync/brotliDecompressSync and the callback forms, params (quality, window,
// mode, size hint), the errors. Brotli 1.2.1 is Node's own version, so the compressed bytes are Node's too
// (printed as their length and CRC-32).
import * as zlib from "node:zlib";

function sample(n: int, seed: int): Buffer {
  const words = ["lorem", "ipsum", "dolor", "sit", "amet", "ação", "日本語", "<div class=\"x\">", "\n", "0123456789"];
  const parts: string[] = [];
  let x: int = seed;
  let len = 0;
  while (len < n) {
    x = (x * 48271) % 2147483647;
    const w = words[x % words.length];
    parts.push(w);
    len += w.length + 1;
  }
  return Buffer.from(parts.join(" "));
}

const inputs: Buffer[] = [Buffer.alloc(0), Buffer.from("hello hello hello"), sample(2000, 3), sample(100000, 77)];
const C = zlib.constants;
for (let k = 0; k < inputs.length; k++) {
  const input = inputs[k];
  const qualities: int[] = [0, 1, 4, 9, 11];
  for (const q of qualities) {
    const c = zlib.brotliCompressSync(input, { params: { [C.BROTLI_PARAM_QUALITY]: q } });
    console.log(`in${k} quality ${q}`, c.length, zlib.crc32(c), zlib.brotliDecompressSync(c).equals(input));
  }
  const d = zlib.brotliCompressSync(input);
  console.log(`in${k} default`, d.length, zlib.crc32(d));
  const w = zlib.brotliCompressSync(input, { params: { [C.BROTLI_PARAM_LGWIN]: 16, [C.BROTLI_PARAM_MODE]: C.BROTLI_MODE_TEXT } });
  console.log(`in${k} lgwin 16 text`, w.length, zlib.crc32(w));
  const h = zlib.brotliCompressSync(input, { params: { [C.BROTLI_PARAM_SIZE_HINT]: input.length, [C.BROTLI_PARAM_QUALITY]: 5 } });
  console.log(`in${k} size hint`, h.length, zlib.crc32(h), zlib.brotliDecompressSync(h).equals(input));
}
console.log(zlib.brotliCompressSync("hello hello hello").toString("hex"));
console.log(zlib.brotliDecompressSync(Buffer.from("GxAA+I2Ubt5EVYaWbCBvAU8cYBw=", "base64")).toString());

function tryIt(label: string, f: () => void): void {
  try {
    f();
    console.log(label, "ok");
  } catch (e: any) {
    console.log(label, e.name, e.code, e.errno ?? "-", e.message);
  }
}
tryIt("not brotli", () => zlib.brotliDecompressSync(Buffer.from("hello world")));
tryIt("truncated", () => zlib.brotliDecompressSync(zlib.brotliCompressSync("hello hello hello").subarray(0, 5)));
tryIt("empty", () => zlib.brotliDecompressSync(Buffer.alloc(0)));
tryIt("bad param", () => zlib.brotliCompressSync("x", { params: { 77: 1 } }));
tryIt("max output", () => zlib.brotliDecompressSync(zlib.brotliCompressSync("a".repeat(1000)), { maxOutputLength: 100 }));

zlib.brotliCompress("callback brotli", (err, c) => {
  console.log("compress", err, c.length);
  zlib.brotliDecompress(c, { params: {} }, (err2, d) => {
    console.log("decompress", err2, d.toString());
    zlib.brotliDecompress(Buffer.from("junk"), (err3, d3) => console.log("error", err3 !== null ? err3.message : "none"));
  });
});
