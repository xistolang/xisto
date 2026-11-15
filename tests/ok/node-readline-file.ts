// readline over fs.createReadStream: a file written with \r\n endings and no final newline, read line by line
// (for await), and the cursor helpers writing ANSI sequences to a stream
import { createReadStream, writeFileSync, unlinkSync } from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import * as readline from "node:readline";
import { Writable } from "node:stream";

const file = path.join(os.tmpdir(), `xisto-readline-${process.pid}.txt`);
const rows: string[] = [];
for (let i = 1; i <= 2000; i++) rows.push(`row ${i};${"x".repeat(i % 50)}`);
writeFileSync(file, rows.join("\r\n"));

let count = 0;
let chars = 0;
let last = "";
const rl = readline.createInterface({ input: createReadStream(file), crlfDelay: Infinity });
for await (const line of rl) {
  count++;
  chars += line.length;
  last = line;
}
console.log(count, chars, last);
unlinkSync(file);

const seen: string[] = [];
const sink = new Writable({
  write: (chunk: any, encoding: string, callback: (error?: Error | null) => void): void => {
    seen.push(JSON.stringify((chunk as Buffer).toString()));
    callback();
  },
});
readline.cursorTo(sink, 3);
readline.cursorTo(sink, 0, 2);
readline.moveCursor(sink, -2, 1);
readline.clearLine(sink, 0);
readline.clearScreenDown(sink);
await new Promise<void>((resolve) => setTimeout(resolve, 5));
console.log(seen.join(" "));
