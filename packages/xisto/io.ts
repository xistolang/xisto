/**
 * Standard input and output as bytes: read all of stdin into one array, or part by part as it arrives; write
 * an array to stdout.
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
 * Compatibility: in Node, `process.stdout.write`, which finishes the write before the program exits. In xisto,
 * a block of 1 MB or more going to a pipe or a file is copied at the call and written by a thread of the runtime
 * while the program goes on (so reading and computing the next part overlap the write); what goes out is what
 * the bytes were at the call, in the program's order with every other output, and all of it before the program
 * exits. Changing the array after the call changes nothing that is written, as in Node.
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

/** Where {@link readStdinParts} cuts standard input: give exactly one of the two. */
export interface StdinPartsOptions {
  /** A new part starts at each occurrence of this character: every part but the first begins with it. */
  splitBefore?: string;
  /** A part ends after each occurrence of this character: every part but the last ends with it. */
  splitAfter?: string;
}

const PART_BLOCK: int = 1 << 22; // bytes asked of each read: a part is given as soon as the next one starts

/**
 * Reads standard input part by part: each part is given as soon as it is complete, while the rest is still
 * being read.
 *
 * The parts are cut at a character: with `splitBefore: ">"`, each part runs from one `>` to the next (the
 * records of a FASTA file); with `splitAfter: "\n"`, each part is a line with its line break. Put together in
 * order, the parts are the whole input, byte for byte.
 *
 * @param options - Where to cut: `splitBefore` or `splitAfter`, one ASCII character.
 * @returns The parts, in input order. Each is a view into the input as read: change it in place freely (the
 *   other parts do not share its bytes); bytes before the first `splitBefore` character are a part of their own.
 * @throws Error when neither or both options are given, or the character is not one ASCII character.
 * @throws Error when stdin cannot be read (it is closed, or is a directory).
 *
 * @remarks
 * Compatibility: the same in Node and in xisto (this file, with `readSync`). With {@link writeStdout} in xisto,
 * the write of one part overlaps the reading of the next. A file given as stdin is read into one array of its
 * size; a pipe, into an array that grows as a part does not end.
 *
 * @example
 * ```ts
 * import { readStdinParts, writeStdout } from "xisto:io";
 * for (const record of readStdinParts({ splitBefore: ">" })) {
 *   writeStdout(record.reverse()); // each record back to front, in order
 * }
 * ```
 */
export function* readStdinParts(options: StdinPartsOptions = {}): Generator<Uint8Array> {
  yield* stdinParts(options, false);
}

/**
 * Reads standard input part by part, as {@link readStdinParts}, but each part is valid only until the next one
 * is asked for: the array the input is read into is used again from its start, so memory holds the largest part
 * (and one read) instead of the whole input.
 *
 * @param options - Where to cut: `splitBefore` or `splitAfter`, one ASCII character.
 * @returns The parts, in input order; a part's bytes are overwritten once the loop goes on to the next.
 * @throws Error as {@link readStdinParts}.
 *
 * @remarks
 * The compiler calls this one in place of {@link readStdinParts} in a `for (const part of readStdinParts(...))`
 * whose part never outlives its iteration (it is only read, changed in place, measured, or given to xisto:bytes
 * functions and {@link writeStdout}, which takes the bytes at the call): the output is the same. Called by hand,
 * keeping a part past its iteration sees later input in it. Compatibility: the same in Node and in xisto.
 *
 * @example
 * ```ts
 * import { readStdinPartsInPlace, writeStdout } from "xisto:io";
 * for (const record of readStdinPartsInPlace({ splitBefore: ">" })) writeStdout(record.reverse());
 * ```
 */
export function* readStdinPartsInPlace(options: StdinPartsOptions = {}): Generator<Uint8Array> {
  yield* stdinParts(options, true);
}

function* stdinParts(options: StdinPartsOptions, inPlace: boolean): Generator<Uint8Array> {
  const { splitBefore = "", splitAfter = "" } = options;
  if ((splitBefore === "") === (splitAfter === "")) {
    throw new Error("readStdinParts: give one of splitBefore or splitAfter");
  }
  const at = splitBefore !== "" ? splitBefore : splitAfter;
  const mark = at.length === 1 ? Math.trunc(at.charCodeAt(0)) : 128; // (a char code is an int)
  if (mark > 127) throw new Error(`readStdinParts: expected one ASCII character to split at, got ${JSON.stringify(at)}`);
  const after = splitAfter !== "";
  const file = fstatSync(0).isFile();
  let buf = new Uint8Array(file ? fstatSync(0).size + 1 : 1 << 20); // (a file: one byte more, so its end is a read of 0)
  let have = 0; // bytes read into buf
  let start = 0; // where the part being read starts
  let scan = 0; // where the search for the next cut goes on
  for (;;) {
    if (inPlace && start > 0) { // the parts before start are done with: what is left of the last read moves to the front
      const left = have - start;
      for (let k = 0; k < left; k++) buf[k] = buf[start + k];
      have = left;
      scan -= start;
      start = 0;
    }
    if (have === buf.length) { // the part has not ended: room for more, keeping it at the front
      const bigger = new Uint8Array(Math.max(buf.length * 2, 1 << 20));
      bigger.set(buf.subarray(start, have));
      have -= start;
      scan -= start;
      start = 0;
      buf = bigger;
    }
    const n = readSync(0, buf, have, Math.min(PART_BLOCK, buf.length - have), null);
    if (n <= 0) break;
    have += n;
    for (;;) {
      const found = buf.subarray(scan, have).indexOf(mark);
      if (found < 0) {
        scan = have;
        break;
      }
      const cut = after ? scan + found + 1 : scan + found;
      if (cut > start) yield buf.subarray(start, cut);
      start = cut;
      scan = scan + found + 1;
    }
  }
  if (have > start) yield buf.subarray(start, have);
}
