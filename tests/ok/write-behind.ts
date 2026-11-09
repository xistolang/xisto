// Big blocks to stdout (1 MB or more: xisto writes them behind, from a copy made at the call) keep their place
// among console.log, process.stdout.write and small blocks, and are what the bytes were at the call.
import { writeStdout } from "xisto:io";

function block(size: int, first: int): Uint8Array {
  const b = new Uint8Array(size);
  for (let i = 0; i < size; i++) b[i] = 97 + ((first + i) % 26);
  b[size - 1] = 10;
  return b;
}

console.log("start");
const big = block(1 << 20, 0);
writeStdout(big);
big.fill(88); // after the call: changes nothing that is written
process.stdout.write("between\n");
writeStdout(block((1 << 20) + 7, 5));
writeStdout(block(10, 1)); // small: written at the call, after the big ones
console.log("after", big[0]);
writeStdout(big); // now all X
for (let k = 0; k < 2; k++) {
  writeStdout(block((1 << 20) + k, k));
  console.log(`k ${k}`);
}
console.log("end");
