// A part written as the last statement of a loop read in place goes out with no copy (writeStdoutLent): the
// output is the same as Node's, and so is a loop that changes the part after writing it (that one keeps the copy).
import { readStdinParts, writeStdout } from "xisto:io";
import { reverse } from "xisto:bytes";

let parts = 0;
for (const part of readStdinParts({ splitBefore: ">" })) {
  reverse(part, { skip: "\n", from: 1 }); // (lent: writeStdout is the last statement)
  parts++;
  writeStdout(part);
}
console.log(`parts: ${parts}`);
