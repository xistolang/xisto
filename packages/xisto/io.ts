/**
 * Standard input and output as bytes: read all of stdin into one array, write an array to stdout.
 *
 * For programs that work on raw bytes (a DNA file, a binary format, text that does not need decoding): no
 * encoding, no line splitting, no stream events; one call to read and one to write.
 *
 * @remarks
 * Compatibility: this file is the module itself. In Node it runs as the reference implementation
 * (`readSync`/`fstatSync` and `process.stdout.write`), with `node --import ./packages/xisto/register.mjs
 * program.ts`; the xisto compiler builds the same file in, with the runtime's file reads. Both give the same
 * bytes.
 *
 * @example
 * ```ts
 * import { readStdin, writeStdout } from "xisto:io";
 * const input = readStdin();
 * writeStdout(input.reverse()); // stdin, back to front
 * ```
 *
 * @packageDocumentation
 * @module xisto:io
 */
import { fstatSync, readSync } from "xisto:fs";

/**
 * Reads all of standard input, until its end, as bytes.
 *
 * A file given as stdin (`program < file`) is read straight into an array of its size; a pipe is read in
 * blocks, into an array that doubles as it fills.
 *
 * @returns The bytes read: an empty array when stdin is empty.
 * @throws Error when stdin cannot be read (it is closed, or is a directory); the message names the system
 * error, and its wording can differ between Node and xisto.
 *
 * @remarks
 * Compatibility: the same in Node and in xisto. The whole input is in memory at once: for input larger than
 * memory, read it in blocks.
 *
 * @example
 * ```ts
 * import { readStdin } from "xisto:io";
 * const input = readStdin();
 * console.log(`${input.length} bytes`);
 * ```
 */
export function readStdin(): Uint8Array {
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

/**
 * Writes bytes to standard output, as they are.
 *
 * No encoding and no line break are added: what is in the array is what goes out.
 *
 * @param bytes - The bytes to write.
 *
 * @remarks
 * Compatibility: in Node, `process.stdout.write`, which finishes the write before the program exits; in xisto,
 * the runtime's write, done when the call returns.
 *
 * @example
 * ```ts
 * import { writeStdout } from "xisto:io";
 * writeStdout(new TextEncoder().encode("hello\n"));
 * ```
 */
export function writeStdout(bytes: Uint8Array): void {
  process.stdout.write(bytes);
}
