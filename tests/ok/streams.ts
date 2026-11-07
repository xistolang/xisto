// Streams: lazy, pull-based, typed. Memory stays at one buffer regardless of the file size.
import { writeFileSync, unlinkSync } from "node:fs";

class Entry {
  level: string;
  n: int;
  constructor(level: string, n: int) {
    this.level = level;
    this.n = n;
  }
}

const path: string = "/tmp/xisto-stream-test.log";
let text: string = "";
for (let i: int = 0; i < 1000; i++) {
  text += `${i % 7 === 0 ? "ERROR" : "INFO"} line ${i}\n`;
}
writeFileSync(path, text + "last line without newline");

const errors: Stream<Entry> = lines(path)
  .filter((l) => l.startsWith("ERROR"))
  .map((l) => new Entry(l.slice(0, 5), Number.isNaN(Number(l.slice(11))) ? -1 : Math.trunc(Number(l.slice(11)))));
let count: int = 0;
let sum: int = 0;
for await (const e of errors) {
  count++;
  sum += e.n;
}
console.log(count, sum);
console.log(lines(path).count(), lines(path).take(3).toArray(), lines(path).filter((l) => l.includes("last")).toArray());
const lens: int[] = lines(path).take(5).map((l) => l.length).toArray();
console.log(lens, lines(path).map((l) => l.length).reduce((acc, n) => acc + n, 0));
const batches: Stream<string[]> = lines(path).take(10).batch(4);
let shapes: int[] = [];
for await (const b of batches) {
  shapes.push(b.length);
}
console.log(shapes, chunks(path, 1000).count(), chunks(path, 1000).map((c) => c.length).reduce((a, n) => a + n, 0));
lines(path).filter((l) => l.startsWith("ERROR")).take(2).forEach((l) => {
  console.log("each", l);
});
lines(path).filter((l) => l.startsWith("ERROR")).map((l) => l.toLowerCase()).writeLines(path + ".out");
console.log(lines(path + ".out").count(), lines(path + ".out").take(1).toArray());
const longest: string = lines(path).reduce((best, l) => (l.length > best.length ? l : best), "");
console.log(longest);
unlinkSync(path);
unlinkSync(path + ".out");
