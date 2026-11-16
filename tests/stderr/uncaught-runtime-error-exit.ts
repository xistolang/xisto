// A runtime error nothing catches, with only 'exit' listeners: they run with the code 1, as in Node (the error
// itself goes to stderr, in xisto's format).
import * as fs from "node:fs";

process.on("exit", (code: number) => console.log("exit", code, process.exitCode ?? "-"));
setTimeout(() => console.log("never"), 50);
console.log("main");
fs.readFileSync("/nonexistent-xisto-dir/b.txt");
console.log("not reached");
