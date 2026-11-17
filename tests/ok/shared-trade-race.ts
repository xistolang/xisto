// Two workers on one SharedArrayBuffer, each writing only bytes of its own (runtime/xisto_rt.h: xs_u8_on_sab).
// Worker A trades short runs (the revcomp loop, compiled to xs_u8_trade): 4 pairs at the two ends of buf[0..31].
// A run below 16 bytes used to be one masked 16-byte block per side, which loads and stores back buf[4..27],
// the bytes in between: worker B's. B keeps filling those with a new value and reading them back; a store of A
// that put back B's older bytes showed as a mismatch (revcomp-5/-6: the first bytes of the next worker's range,
// a wrong output now and then). In Node, and in xisto since the fix, A writes none of B's bytes.
import { Worker, isMainThread, parentPort } from "node:worker_threads";

const ROUNDS: int = 300000;

function complementTable(): Uint8Array {
  const t = new Uint8Array(256);
  for (let i = 0; i < 256; i++) t[i] = i;
  const pairs = new TextEncoder().encode("ATCGGCTAUAMKRYWWSSYRKMVBHDDHBVNN");
  for (let i = 0; i < pairs.length; i += 2) {
    t[pairs[i]] = pairs[i + 1];
    t[pairs[i] + 32] = pairs[i + 1];
  }
  return t;
}
const table = complementTable();

// symbols a..a+run-1 traded (complemented) with b..b-run+1, as revcomp-6's trade does it
function trade(buf: Uint8Array, a: int, b: int, run: int): void {
  for (let t = 0; t < run; t++) {
    const x = buf[a + t];
    buf[a + t] = table[buf[b - t]];
    buf[b - t] = table[x];
  }
}

function sideA(buf: Uint8Array, flags: Int32Array): string {
  const base = buf.length - 32, run = buf.length >> 3; // 0 and 4, known only when it runs (a call with constants is
  for (let r = 0; r < ROUNDS; r++) {                   // compiled as a loop of its own, not as the trade)
    trade(buf, base, base + 31, run);
    if (Atomics.load(flags, 2) === 1) break; // B is done: nothing left to overlap with
  }
  // trades back and forth: the ends are either as they were or traded once
  const d = new TextDecoder();
  const ends = `${d.decode(buf.slice(0, 4))} ${d.decode(buf.slice(28, 32))}`;
  return `A: ${ends === "ACGT TGCA" || ends === "TGCA ACGT" ? "ends ok" : "ends wrong: " + ends}`;
}

function sideB(buf: Uint8Array, flags: Int32Array): string {
  let lost = 0;
  for (let r = 0; r < ROUNDS; r++) {
    const v = 1 + (r % 200);
    for (let k = 4; k < 28; k++) buf[k] = v;
    Atomics.add(flags, 3, 1); // (a fence: the reads below go to memory, not to what this thread just stored)
    for (let k = 4; k < 28; k++) if (buf[k] !== v) lost++;
  }
  Atomics.store(flags, 2, 1);
  return `B: bytes of B written over by A: ${lost}`;
}

if (isMainThread) {
  const sab = new SharedArrayBuffer(64);
  const buf = new Uint8Array(sab, 0, 32);
  buf.set(new TextEncoder().encode("ACGT"), 0);
  buf.set(new TextEncoder().encode("TGCA"), 28);
  const lines: string[] = [];
  const workers: Worker[] = [];
  for (let w = 0; w < 2; w++) {
    const wk = new Worker(__filename);
    wk.on("message", (m: string) => {
      lines.push(m);
      if (lines.length === 2) {
        lines.sort();
        for (const l of lines) console.log(l);
        for (const k of workers) k.terminate();
      }
    });
    wk.postMessage(sab);
    workers.push(wk);
  }
} else {
  parentPort.on("message", (sab: SharedArrayBuffer) => {
    const buf = new Uint8Array(sab, 0, 32);
    const flags = new Int32Array(sab, 32, 8);
    const role = Atomics.add(flags, 0, 1); // the first worker here is A, the second B
    Atomics.add(flags, 1, 1);
    while (Atomics.load(flags, 1) < 2) {} // both start together
    parentPort.postMessage(role === 0 ? sideA(buf, flags) : sideB(buf, flags));
  });
}
