// Errors thrown after the top level is done, taken by process.on('uncaughtException'): a 'beforeExit' listener that
// throws (Node emits 'beforeExit' again), an 'unhandledRejection' listener that throws (the program goes on: the
// timer still fires) and an 'exit' listener that throws. Each reaches the listener once, as in Node.
let n = 0;
process.on("uncaughtException", (e: Error, origin: string) => console.log("caught", e.message, origin));
process.on("unhandledRejection", (r: unknown) => {
  console.log("rejection", String(r));
  throw new Error("from the rejection listener");
});
process.on("beforeExit", (code: number) => {
  n++;
  console.log("beforeExit", code, n);
  if (n < 3) throw new Error(`beforeExit ${n}`);
});
process.on("exit", (code: number) => {
  console.log("exit", code);
  throw new Error("from exit");
});
Promise.reject(new Error("r1"));
setTimeout(() => console.log("timer"), 5);
console.log("main");
