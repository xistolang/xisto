/*  The Computer Language Benchmarks Game
    https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

    contributed by Joe Farro
    parts taken from solution contributed by
    Jos Hirth which was modified by 10iii
    (xisto: the same program with types, the counts as int; node/revcomp-node-2.js is the original. There stdin.read(16000) is
    called on 'readable' events; here standard input is read at once and handed out the way the stream does
    (see stdinRead). LinkedArray is a class, its lines an array of LA_LEN chunks, and
    `if (line)` past the first line is `lnIdx >= 0`)
*/

import { readFileSync } from "node:fs";

// standard input as Node's stream hands it to the original: the data arrives in 64 KB blocks, each arrival is
// a 'readable' event, and stdin.read(n) gives null while fewer than n bytes are buffered; the end is known one
// event later (a read of 0 bytes), and then read(n) gives what is left
const input = readFileSync(0);
let inputPos: int = 0;
let arrived: int = 0;
let ended = false;
function stdinRead(size: int): Buffer | null {
  if (inputPos >= arrived || (!ended && arrived - inputPos < size)) return null;
  const end = Math.min(inputPos + size, arrived);
  const c = input.subarray(inputPos, end);
  inputPos = end;
  return c;
}

const READ_SIZE: int = 16000;
const writeBuffer = Buffer.allocUnsafe(READ_SIZE + (READ_SIZE / 61 | 0));
let metaI: int = 0;
let numLines: int = 0;

const LA_LEN: int = 30;

class LinkedArray {
  prev: LinkedArray | null;
  next: LinkedArray | null = null;
  pos: int = 0;
  data: Buffer[];
  constructor(prev: LinkedArray | null) {
    this.prev = prev;
    this.data = new Array<Buffer>(LA_LEN).fill(Buffer.alloc(0));
  }
}

function getCompChar(code: int): int {
  switch (code) {
    case 65:
    case 97: return 84;
    case 66:
    case 98: return 86;
    case 67:
    case 99: return 71;
    case 68:
    case 100: return 72;
    case 71:
    case 103: return 67;
    case 72:
    case 104: return 68;
    case 75:
    case 107: return 77;
    case 77:
    case 109: return 75;
    case 78:
    case 110: return 78;
    case 82:
    case 114: return 89;
    case 83:
    case 115: return 83;
    case 84:
    case 85: return 65;
    case 86:
    case 118: return 66;
    case 87:
    case 119: return 87;
    case 116:
    case 117: return 65;
    case 89:
    case 121: return 82;
  }
  return 0; // (undefined there, stored as 0 in the Buffer)
}

function reverseCompPrint(line: Buffer): void {
  const _metaI = metaI;
  let _numLines = numLines;
  let count = 0;
  const target = writeBuffer;
  const len = line.length;
  const right = line.length - 1;

  let ileft = 0;
  let iright = 0;

  let c: int = 0;
  while (iright < len) {
    c = line[right - iright];
    iright++;
    if (c === 10) {
      // skip linebreaks
      if (iright === len) {
        break;
      }
      c = line[right - iright];
      iright++;
    }
    target[ileft] = getCompChar(c);
    ileft++;
    count++;
    if ((count + _metaI - _numLines) % 60 === 0) {
      // need a linebreak
      target[ileft] = 10;
      ileft++;
      count++;
      _numLines++;
    }
  }
  metaI = _metaI + count;
  numLines = _numLines;
  process.stdout.write(target.slice(0, count).toString("ascii"));
}

function reverse(la_: LinkedArray): void {
  // reset the metaI and numLines in this section
  metaI = 0;
  numLines = 0;

  let la: LinkedArray = la_;
  let lines = la.data;
  let lnIdx = la.pos - 1;
  let line = lines[lnIdx];

  while (true) {
    reverseCompPrint(line);
    lnIdx--;
    if (lnIdx >= 0) {
      line = lines[lnIdx];
      continue;
    }
    const prev = la.prev;
    if (prev === null) {
      break;
    }
    la = prev;
    lines = la.data;
    lnIdx = la.pos;
    lnIdx--;
    line = lines[lnIdx];
  }
  if ((metaI - numLines) % 60 !== 0) {
    process.stdout.write("\n");
  }
}

const headLA = new LinkedArray(null);
let la = headLA;
let lnIdx: int = 0;
let lines = la.data;

let needHeader = true;
let headerPartial = Buffer.alloc(0);
let isFirst = true;

function read(): void {
  let chunk = stdinRead(READ_SIZE);
  let isFinal = false;
  if (chunk === null) {
    if (isFirst) {
      isFirst = false;
      return;
    }
    la.pos = lnIdx;
    reverse(la);
    return;
  } else if (chunk.length < READ_SIZE) {
    isFinal = true;
  }
  while (chunk !== null) {
    let ch: Buffer = chunk;
    while (true) {
      // if have read a partial header line, read the rest of it
      if (needHeader) {
        const headerEnds = ch.indexOf("\n");
        console.log(headerPartial.toString("ascii") + ch.slice(0, headerEnds).toString("ascii"));
        headerPartial = Buffer.alloc(0);
        ch = ch.slice(headerEnds);
        needHeader = false;
      }
      const caretIdx = ch.indexOf(">");
      if (caretIdx > -1) {
        // there is a caret in this chunk -- process the first part of
        // the chunk and then continue the `while (true)` loop to process
        // the next part of the chunk
        lines[lnIdx] = ch.slice(0, caretIdx);
        // set chunk to the next section
        ch = ch.slice(caretIdx);
        la.pos = lnIdx + 1;
        reverse(la);
        // reset the data holders
        la = headLA;
        lines = la.data;
        la.pos = 0;
        lnIdx = 0;
        // check to see if the entire header line is here
        const headerEnds = ch.indexOf("\n");
        if (headerEnds < 0) {
          needHeader = true;
          headerPartial = ch;
          break;
        } else {
          // log the header
          console.log(ch.slice(0, headerEnds).toString("ascii"));
          // continue processing the rest of the chunk
          ch = ch.slice(headerEnds);
        }
      } else {
        if (lnIdx === LA_LEN) {
          la.pos = LA_LEN;
          if (la.next === null) la.next = new LinkedArray(la);
          la = la.next!;
          lines = la.data;
          lines[0] = ch;
          lnIdx = la.pos = 1;
        } else {
          lines[lnIdx] = ch;
          lnIdx++;
        }
        break;
      }
    }
    chunk = stdinRead(READ_SIZE);
  }
  if (isFinal) {
    la.pos = lnIdx;
    reverse(la);
  }
}

while (arrived < input.length) {
  arrived = Math.min(arrived + 65536, input.length);
  read();
}
ended = true;
read(); // (the 'readable' event at the end of the stream)
