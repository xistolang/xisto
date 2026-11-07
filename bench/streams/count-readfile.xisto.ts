// Count ERROR lines by reading the whole file: memory is the file size.
import { readFileSync } from "node:fs";
const text: string = readFileSync(process.argv[2], "utf8");
let n: int = 0;
let pos: int = 0;
while (true) {
  pos = text.indexOf("ERROR", pos);
  if (pos < 0) {
    break;
  }
  n++;
  pos += 5;
}
console.log(n);
