// node:readline over process.stdin (a file here: node-readline.stdin): 'line' for each line ending in \n, \r\n or a
// lone \r, the last line even with no newline, then 'close'; the input is not a terminal
import * as readline from "node:readline";
import * as tty from "node:tty";

console.log("stdin is a tty:", tty.isatty(0), process.stdin.isTTY === true);
const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
let n = 0;
rl.on("line", (line: string) => {
  n++;
  console.log(`${n}: [${line}] ${line.length}`);
});
rl.on("close", () => {
  console.log("closed after", n, "lines");
});
console.log("listening");
