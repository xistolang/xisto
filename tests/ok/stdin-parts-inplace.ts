// readStdinParts read in place (inplace.c): this loop's part stays in its iteration (read, changed in place, given
// to xisto:bytes and writeStdout), so every part is read into the same array; the output is Node's.
import { readStdinParts, writeStdout } from "xisto:io";
import { byteMap, indexOf, reverse } from "xisto:bytes";

const UPPER = byteMap("abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
let parts = 0;
for (const part of readStdinParts({ splitBefore: ">" })) {
  const body = indexOf(part, "\n") + 1;
  if (part.length > 0 && part[0] === 62) part[0] = 60; // ">" -> "<"
  reverse(part, { map: UPPER, skip: "\n", from: body });
  writeStdout(part);
  parts++;
}
console.log(`\n${parts} parts`);
