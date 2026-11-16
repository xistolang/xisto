// A runtime error nothing catches (a failed readFileSync, JSON.parse of bad text) goes to
// process.on('uncaughtException') as a thrown one does, with Node's fields (code, errno, syscall, path); the
// program goes on. (uncaught-runtime-error-exit: with only 'exit' listeners, the program ends with code 1.)
import * as fs from "node:fs";

process.on("uncaughtException", (e: any, origin: string) => {
  const p: string = e.path ?? "-";
  console.log("caught:", e.message, e.code ?? "-", e.errno ?? "-", e.syscall ?? "-", p, origin, e instanceof Error);
});
process.on("exit", (code: number) => console.log("exit", code));

setTimeout(() => {
  console.log("timer");
  const v: Record<string, number> = JSON.parse("{");
  console.log("not reached", v);
  console.log("not reached");
}, 1);
setTimeout(() => {
  fs.readFileSync("/nonexistent-xisto-dir/a.txt", "utf8");
  console.log("not reached");
}, 5);
setTimeout(() => console.log("still running"), 10);
console.log("main");
fs.readFileSync("/nonexistent-xisto-dir/b.txt");
console.log("not reached");
