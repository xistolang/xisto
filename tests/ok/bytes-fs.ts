// Binary files: writeFileSync with bytes, readFileSync without an encoding gives a Buffer
import { readFileSync, writeFileSync, unlinkSync } from "node:fs";
const data = new Uint8Array(256);
for (let i: int = 0; i < 256; i++) {
  data[i] = 255 - i;
}
writeFileSync("xisto-bytes-test.bin", data);
const back = readFileSync("xisto-bytes-test.bin");
console.log(back.length, back[0], back[255], back.subarray(0, 4), back.equals(Buffer.from(data)));
writeFileSync("xisto-bytes-test.bin", Buffer.from("texto ✓"));
console.log(readFileSync("xisto-bytes-test.bin", "utf8"), readFileSync("xisto-bytes-test.bin").toString("hex"));
unlinkSync("xisto-bytes-test.bin");
