// The web streams as globals (no import): ReadableStream<T> with start/pull/cancel, its controller's enqueue/close/
// error, getReader().read()/releaseLock(), for await; WritableStream and TransformStream, pipeThrough/pipeTo
const rs = new ReadableStream<string>({
  start: (c) => {
    c.enqueue("a");
    c.enqueue("b");
    c.close();
  },
});
async function main(): Promise<void> {
  const r = rs.getReader();
  const out: string[] = [];
  for (;;) {
    const { done, value } = await r.read();
    if (done) break;
    out.push(value!);
  }
  r.releaseLock();
  console.log(out, rs.locked);

  // pull on demand, until the source closes
  let n = 0;
  const counter = new ReadableStream<number>({
    pull: (c) => {
      n++;
      if (n <= 3) c.enqueue(n * 10);
      else c.close();
    },
  });
  const got: number[] = [];
  for await (const v of counter) got.push(v as number);
  console.log(got);

  // an error from the source rejects the read
  const bad = new ReadableStream<string>({ start: (c) => c.error(new Error("broken")) });
  try {
    await bad.getReader().read();
  } catch (e) {
    console.log("read failed:", (e as Error).message);
  }

  // cancel reaches the source's cancel
  const cancellable = new ReadableStream<string>({ cancel: (reason) => console.log("cancelled:", reason) });
  await cancellable.cancel("enough");

  // a TransformStream between a ReadableStream and a WritableStream
  const written: string[] = [];
  const sink = new WritableStream<string>({
    write: (chunk) => {
      written.push(chunk as string);
    },
    close: () => console.log("sink closed"),
  });
  const upper = new TransformStream<string, string>({
    transform: (chunk, c) => c.enqueue((chunk as string).toUpperCase()),
  });
  const src = new ReadableStream<string>({
    start: (c) => {
      c.enqueue("x");
      c.enqueue("y");
      c.close();
    },
  });
  await src.pipeThrough(upper).pipeTo(sink);
  console.log(written);
  const w = new WritableStream<string>({ write: (chunk) => console.log("wrote", chunk) });
  const writer = w.getWriter();
  await writer.write("one");
  await writer.close();
  console.log("done");
}
main();
