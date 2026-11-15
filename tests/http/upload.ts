// fetch with AbortSignal.timeout and a body larger than the socket's buffers, to a server that accepts the
// connection and never reads it: the write must give up at the deadline (TimeoutError), not wait forever
async function main(): Promise<void> {
  const body = "x".repeat(4 << 20);
  const t0 = Date.now();
  try {
    await fetch("http://127.0.0.1:8095/upload", { method: "POST", body: body, signal: AbortSignal.timeout(300) });
    console.log("upload: answered");
  } catch (e) {
    const ms = Date.now() - t0;
    console.log("upload:", (e as Error).name, ms < 2000 ? "within the limit" : `LATE (${ms} ms)`);
  }
}
main();
