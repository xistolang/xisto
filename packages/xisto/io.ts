// xisto:io: stdin and stdout as bytes.
// This file is both: the module the xisto compiler builds in (its loops become the runtime's block reads), and
// the reference implementation Node runs (node --import ./packages/xisto/register.mjs), with the same output.
import { fstatSync, readSync } from "xisto:fs";

// All of stdin as bytes: a file is read straight into a buffer of its size; a pipe in blocks into a buffer that
// doubles as it fills.
export function stdinBytes(): Uint8Array {
  if (fstatSync(0).isFile()) {
    const buf = new Uint8Array(fstatSync(0).size);
    let have = 0;
    while (have < buf.length) {
      const n = readSync(0, buf, have, buf.length - have, null);
      if (n <= 0) break;
      have += n;
    }
    return have === buf.length ? buf : buf.subarray(0, have);
  }
  let buf = new Uint8Array(1 << 20);
  let have = 0;
  for (;;) {
    if (have === buf.length) {
      const bigger = new Uint8Array(buf.length * 2);
      bigger.set(buf);
      buf = bigger;
    }
    const n = readSync(0, buf, have, buf.length - have, null);
    if (n <= 0) break;
    have += n;
  }
  return buf.subarray(0, have);
}

// Writes bytes to stdout, as they are.
export function writeStdout(bytes: Uint8Array): void {
  process.stdout.write(bytes);
}
