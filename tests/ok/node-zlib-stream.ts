// node:zlib's streams: createGzip/createGunzip, createDeflate/createInflate, createDeflateRaw/createInflateRaw,
// createUnzip and the Brotli ones, between a Readable and a Writable (the output collected and checked at 'finish').
import { Readable, Writable } from "node:stream";
import type { TransformCallback } from "node:stream";
import * as zlib from "node:zlib";

class Collect extends Writable {
  parts: Uint8Array[] = [];
  override _write(chunk: Uint8Array, encoding: string, callback: TransformCallback): void {
    this.parts.push(chunk);
    callback(null);
  }
  all(): Buffer {
    return Buffer.concat(this.parts);
  }
}

const text = "streamed text, ação, 日本語. ".repeat(200);
const pieces: Uint8Array[] = [];
for (let i = 0; i < text.length; i += 333) pieces.push(Buffer.from(text.slice(i, i + 333)));

const pairs: string[] = ["gzip", "deflate", "deflateRaw", "brotli", "unzip"];
function compressor(name: string): zlib.ZlibStream {
  if (name === "gzip") return zlib.createGzip();
  if (name === "deflate") return zlib.createDeflate({ level: 9 });
  if (name === "deflateRaw") return zlib.createDeflateRaw();
  if (name === "brotli") return zlib.createBrotliCompress();
  return zlib.createGzip({ level: 1 });
}
function decompress(name: string, b: Buffer): Buffer {
  if (name === "gzip") return zlib.gunzipSync(b);
  if (name === "deflate") return zlib.inflateSync(b);
  if (name === "deflateRaw") return zlib.inflateRawSync(b);
  if (name === "brotli") return zlib.brotliDecompressSync(b);
  return zlib.unzipSync(b);
}
function decompressor(name: string): zlib.ZlibStream {
  if (name === "gzip") return zlib.createGunzip();
  if (name === "deflate") return zlib.createInflate();
  if (name === "deflateRaw") return zlib.createInflateRaw();
  if (name === "brotli") return zlib.createBrotliDecompress();
  return zlib.createUnzip();
}

function round(k: int): void {
  if (k >= pairs.length) {
    errorCase();
    return;
  }
  const name = pairs[k];
  const sink = new Collect();
  sink.on("finish", (): void => {
    const packed = sink.all();
    console.log(name, decompress(name, packed).toString() === text, zlib.crc32(packed));
    const back = new Collect();
    back.on("finish", (): void => {
      console.log(name, "back", back.all().toString() === text);
      round(k + 1);
    });
    Readable.from([packed.subarray(0, 10), packed.subarray(10, packed.length)]).pipe(decompressor(name)).pipe(back);
  });
  Readable.from(pieces).pipe(compressor(name)).pipe(sink);
}

function errorCase(): void {
  const g = zlib.createGunzip();
  g.on("error", (err: Error): void => {
    console.log("error:", err.message);
  });
  g.write(Buffer.from("not gzip at all"));
  g.end();
}

round(0);
