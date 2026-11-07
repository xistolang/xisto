// Node: readline over a read stream (the idiomatic streaming way).
import { createReadStream } from "node:fs";
import { createInterface } from "node:readline";
let n = 0;
const rl = createInterface({ input: createReadStream(process.argv[2], { highWaterMark: 64 * 1024 }), crlfDelay: Infinity });
for await (const line of rl) {
  if (line.includes("ERROR")) n++;
}
console.log(n);
