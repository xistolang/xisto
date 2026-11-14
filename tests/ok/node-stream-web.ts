// node:stream/web: ReadableStream with start/pull and a reader, WritableStream with an async sink, TransformStream
// through pipeThrough/pipeTo, TextEncoderStream/TextDecoderStream, a writer, tee, an errored stream.
import { ReadableStream, WritableStream, TransformStream, TextEncoderStream, TextDecoderStream } from "node:stream/web";
import type { ReadableStreamDefaultController, TransformStreamDefaultController } from "node:stream/web";

async function main(): Promise<void> {
  let n = 0;
  const rs = new ReadableStream({
    start: (c: ReadableStreamDefaultController): void => {
      console.log("start");
      c.enqueue("a");
    },
    pull: (c: ReadableStreamDefaultController): void => {
      n++;
      if (n === 3) console.log("last pull");
      if (n < 3) c.enqueue("p" + n);
      else c.close();
    },
  });
  console.log("constructed", rs.locked);
  const reader = rs.getReader();
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    console.log("read", value);
  }
  reader.releaseLock();
  console.log("locked", rs.locked);

  const out: string[] = [];
  const ws = new WritableStream({
    write: async (chunk: any): Promise<void> => {
      await new Promise<void>((r) => setTimeout(r, 1));
      out.push(String(chunk));
    },
    close: (): void => {
      console.log("sink close", out.join(","));
    },
  });
  const upper = new TransformStream({
    transform: (chunk: any, c: TransformStreamDefaultController): void => {
      c.enqueue(String(chunk).toUpperCase());
    },
    flush: (c: TransformStreamDefaultController): void => {
      c.enqueue("END");
    },
  });
  const src = new ReadableStream({
    start: (c: ReadableStreamDefaultController): void => {
      c.enqueue("x");
      c.enqueue("y");
      c.close();
    },
  });
  await src.pipeThrough(upper).pipeTo(ws);
  console.log("piped", out.join(","));

  const bytes = new ReadableStream({
    start: (c: ReadableStreamDefaultController): void => {
      c.enqueue("héllo ");
      c.enqueue("wörld");
      c.close();
    },
  });
  const parts: string[] = [];
  for await (const s of bytes.pipeThrough(new TextEncoderStream()).pipeThrough(new TextDecoderStream())) parts.push(s as string);
  console.log("text", parts.join("|"));

  const w2 = new WritableStream({
    write: (chunk: any): void => {
      console.log("w2 write", chunk);
    },
  });
  const writer = w2.getWriter();
  await writer.ready;
  console.log("desired", writer.desiredSize);
  await writer.write("one");
  await writer.write("two");
  await writer.close();
  console.log("w2 closed");

  const [t1, t2] = new ReadableStream({
    start: (c: ReadableStreamDefaultController): void => {
      c.enqueue(1);
      c.enqueue(2);
      c.close();
    },
  }).tee();
  const r1 = t1.getReader();
  const r2 = t2.getReader();
  console.log("tee", (await r1.read()).value, (await r2.read()).value, (await r1.read()).value, (await r2.read()).value, (await r1.read()).done);

  const errs = new ReadableStream({
    start: (c: ReadableStreamDefaultController): void => {
      c.error(new Error("broken"));
    },
  });
  try {
    await errs.getReader().read();
  } catch (e) {
    console.log("caught", (e as Error).message);
  }
}
main();
