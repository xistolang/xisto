// an error thrown by the top level with an 'uncaughtException' listener: the rest of the top level is skipped and
// the event loop runs what it had already. The program is an ES module, as Node runs a .ts file with an import or
// an export: its top level's error is the module's rejection (origin "unhandledRejection"; a CommonJS file would
// report "uncaughtException").
export {};
process.on("uncaughtException", (err: Error, origin: string) => {
  console.log("caught:", err.message, origin);
});
process.on("beforeExit", (code: number) => console.log("beforeExit", code));
process.on("exit", (code: number) => console.log("exit", code));
const names: string[] = ["x", "y"];
setTimeout(() => console.log("timer still runs", names.length), 5);
console.log("top level");
function fail(n: number): void {
  const msg = `top-level ${n}`;
  if (n > 0) throw new Error(msg);
}
fail(0);
fail(1);
console.log("not reached");
