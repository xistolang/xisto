// xisto:stream/consumers (node:stream/consumers): the whole content of a readable stream as text, JSON, a Buffer or
// bytes, written in xisto over runtime/node/stream.ts.
// Differences from Node: arrayBuffer() gives a Uint8Array (xisto has no ArrayBuffer value; the bytes are the same);
// blob() is not there (no Blob). The stream is a Readable (Node also takes any async iterable).

import { Readable } from "xisto:stream";

async function chunks(stream: Readable): Promise<Buffer> {
  const parts: Buffer[] = [];
  for await (const chunk of stream) {
    if (typeof chunk === "string") parts.push(Buffer.from(chunk as string));
    else parts.push(Buffer.from(chunk as Uint8Array));
  }
  return Buffer.concat(parts);
}

/** The stream's content decoded as UTF-8. */
export async function text(stream: Readable): Promise<string> {
  const b = await chunks(stream);
  return b.toString("utf8");
}

/** The stream's content parsed as JSON. */
export async function json(stream: Readable): Promise<any> {
  const s = await text(stream);
  return JSON.parse(s);
}

/** The stream's content as one Buffer. */
export async function buffer(stream: Readable): Promise<Buffer> {
  return await chunks(stream);
}

/** The stream's content as a Uint8Array. */
export async function bytes(stream: Readable): Promise<Uint8Array> {
  const b = await chunks(stream);
  return new Uint8Array(b);
}

/** The stream's content as bytes (Node: an ArrayBuffer; xisto has no ArrayBuffer value, so a Uint8Array). */
export async function arrayBuffer(stream: Readable): Promise<Uint8Array> {
  const b = await chunks(stream);
  return new Uint8Array(b);
}
