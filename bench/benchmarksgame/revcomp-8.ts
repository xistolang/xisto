/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

reverse-complement, xisto #8: each sequence read backwards. The reverse complement of a sequence starts with the
complement of its last symbols, so when stdin is a file (it can be read at any position) the output is made a
block of lines at a time from the end of the sequence towards its start: memory holds one block (~2 MB), not
the sequence (~120 MB for the largest of the 250 MB input), and the first lines go out as soon as the end of the
sequence is known. The output keeps the input's lines (60 symbols each, the last one shorter), as #1 does:
a block of output lines is the block of input lines at the mirror place, read into memory, its symbols turned
around the newlines (reverse of xisto:bytes, with the complement map) and, when the sequence's length is not a
multiple of 60, each line's newline moved to its end. When stdin is not a file (a pipe), it is read whole first
and the same blocks are taken from memory.
 */

import { fstatSync, readFileSync, readSync } from "node:fs";
import { byteMap, reverse } from "xisto:bytes";

const COMPLEMENT = byteMap("ACGTUMRWSYKVHDBN", "TGCAAKYWSRMBDHVN", { ignoreCase: true });
const LINE: int = 60; // symbols per line (the input's lines are assumed to be 60 long, the last one shorter)
const LINES: int = 32768; // output lines per write (~2 MB: big writes go out behind, while the next block is made)
const PIECE: int = 8192; // lines turned at a time (~0,5 MB: reverse keeps a piece below 1 MB on one thread)
const SPAN: int = (LINE + 1) * LINES;

let size: int = 0;
let whole = new Uint8Array(0); // stdin read whole, when it is not a file

// len bytes of the input from position pos into buf at off
function readAt(buf: Uint8Array, off: int, len: int, pos: int): void {
  if (whole.length > 0) {
    buf.set(whole.subarray(pos, pos + len), off);
    return;
  }
  let done: int = 0;
  while (done < len) {
    const got = readSync(0, buf, off + done, len - done, pos + done);
    if (got <= 0) throw new Error("stdin ended early");
    done += got;
  }
}

// the position of the first '>' at from or after it, or the input's size
const scan = new Uint8Array(1 << 20);
function nextHeader(from: int): int {
  let pos: int = from;
  while (pos < size) {
    const n: int = Math.min(scan.length, size - pos);
    readAt(scan, 0, n, pos);
    const at = scan.subarray(0, n).indexOf(62);
    if (at >= 0) return pos + at;
    pos += n;
  }
  return size;
}

// where the input symbol i of a sequence is, from the start of its body
function posOf(i: int): int {
  return i + Math.trunc(i / LINE);
}

const ROOM: int = 1 << 16; // for the header, written with the first block
const buf = new Uint8Array(ROOM + SPAN + 1);
let head = new Uint8Array(4096);

// the sequence in [h, e): its header line as it is, then its body turned
function sequence(h: int, e: int): void {
  // the header line (to the first newline)
  let hl: int = -1;
  let have: int = 0;
  while (hl < 0 && h + have < e) {
    if (have === head.length) {
      const bigger = new Uint8Array(head.length * 2);
      bigger.set(head);
      head = bigger;
    }
    const n: int = Math.min(head.length - have, e - h - have);
    readAt(head, have, n, h + have);
    const nl = head.subarray(have, have + n).indexOf(10);
    if (nl >= 0) hl = have + nl + 1;
    have += n;
  }
  if (hl < 0) { // no body: the part is all header
    process.stdout.write(head.subarray(0, e - h));
    return;
  }
  const b: int = h + hl; // the body
  const len: int = e - b;
  if (len === 0) {
    process.stdout.write(head.subarray(0, hl));
    return;
  }
  readAt(buf, 0, 1, e - 1);
  const endsNl = buf[0] === 10;
  const lines: int = endsNl ? Math.trunc((len + LINE) / (LINE + 1)) : Math.trunc(len / (LINE + 1)); // newlines in it
  const n: int = len - lines; // symbols
  const r: int = n % LINE; // in the last line (0: it is full)
  let first = hl <= ROOM;
  if (!first) process.stdout.write(head.subarray(0, hl)); // (a header too long to go with the first block)
  let end: int = 0; // bytes of the block being made
  if (first) {
    buf.set(head.subarray(0, hl), 0);
    end = hl;
  }
  for (let k0: int = 0; k0 < n; k0 += LINE * PIECE) {
    const k1: int = Math.min(n, k0 + LINE * PIECE); // output symbols [k0, k1): input symbols [n - k1, n - k0)
    const p0: int = posOf(n - k1), p1: int = posOf(n - k0 - 1) + 1;
    const off: int = end;
    readAt(buf, off, p1 - p0, b + p0);
    reverse(buf, { map: COMPLEMENT, skip: "\n", from: off, to: off + (p1 - p0) });
    end = off + (p1 - p0);
    if (k1 < n && r !== 0) {
      // the piece starts r symbols into an input line: each output line is that line's tail, its newline, the
      // next line's first r symbols; the newline moves to the end
      for (let q: int = off; q < end; q += LINE + 1) {
        for (let t: int = q + LINE - r; t < q + LINE; t++) buf[t] = buf[t + 1];
        buf[q + LINE] = 10;
      }
    } else if (k1 < n || endsNl) {
      buf[end] = 10; // the newline after the piece's last line (the input's is past the piece)
      end++;
    }
    if (end > buf.length - (LINE + 1) * PIECE - 1 || k1 === n) { // a block's worth (or the sequence's end): out
      process.stdout.write(buf.subarray(0, end));
      end = 0;
    }
  }
}
function main(): void {
  const st = fstatSync(0);
  if (st.isFile() && st.size > 0) {
    size = st.size;
    const one = new Uint8Array(1);
    readSync(0, one, 0, 1, null); // (read in order, as a reader of stdin: the writes then go out behind)
  } else {
    whole = readFileSync(0);
    size = whole.length;
  }
  let start: int = 0;
  while (start < size) {
    const next = nextHeader(start + 1);
    sequence(start, next);
    start = next;
  }
}

main();
