// PolyBench/C 4.2.1 (utilities/polybench.h, polybench.c), the parts the ports use: the dataset, the timer and the
// dump of the arrays.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
//
// Command line of every port: [MINI|SMALL|MEDIUM|LARGE|EXTRALARGE] [time]
//   the dataset (-D<NAME>_DATASET in C; LARGE when absent) and "time" (-DPOLYBENCH_TIME: the kernel's seconds,
//   "%0.6f\n" on stdout). The dump (-DPOLYBENCH_DUMP_ARRAYS) always goes to stderr, as POLYBENCH_DUMP_TARGET.

export const DATASETS = ["MINI", "SMALL", "MEDIUM", "LARGE", "EXTRALARGE"];

// the dataset's position in DATASETS (each kernel keeps its sizes in that order)
export function dataset(): int {
  for (let i = 2; i < process.argv.length; i++) {
    const k = DATASETS.indexOf(process.argv[i]);
    if (k >= 0) return k;
  }
  return 3;
}

let timing = false;
let start = 0;
let stop = 0;

export function timerStart(): void {
  timing = process.argv.includes("time");
  start = performance.now();
}

export function timerStop(): void {
  stop = performance.now();
}

export function timerPrint(): void {
  if (timing) console.log(((stop - start) / 1000).toFixed(6));
}

// "nodump" on the command line: the timing run, as C built without -DPOLYBENCH_DUMP_ARRAYS (the call stays, so
// the kernel's results stay live)
export function dumpArrays(): boolean {
  return !process.argv.includes("nodump");
}

// stderr through a buffer, as a FILE*: one write per 64 KB instead of one per number
let buf = "";

export function dumpWrite(s: string): void {
  buf += s;
  if (buf.length >= 65536) {
    process.stderr.write(buf);
    buf = "";
  }
}

export function dumpStart(): void {
  dumpWrite("==BEGIN DUMP_ARRAYS==\n");
}

export function dumpFinish(): void {
  dumpWrite("==END   DUMP_ARRAYS==\n");
  process.stderr.write(buf);
  buf = "";
}

// what is still in the buffer (fdtd-2d dumps two arrays after POLYBENCH_DUMP_FINISH)
export function dumpFlush(): void {
  process.stderr.write(buf);
  buf = "";
}

export function dumpBegin(name: string): void {
  dumpWrite("begin dump: " + name);
}

export function dumpEnd(name: string): void {
  dumpWrite("\nend   dump: " + name + "\n");
}

// C's "%0.2lf": toFixed(2) except where the two differ. A tie (the double is exactly halfway between two
// hundredths: x = odd/8, .125 .375 .625 .875) goes to the even hundredth in C and away from zero in toFixed;
// -0 prints "-0.00" in C and "0.00" in toFixed. (Not covered: |x| >= 1e21, where toFixed switches to exponent
// form, and NaN, printed "nan" by C.)
export function fmt2(x: number): string {
  if (x === 0) return 1 / x < 0 ? "-0.00" : "0.00";
  if (x === Infinity) return "inf";
  if (x === -Infinity) return "-inf";
  const e = Math.abs(x) * 8;
  if (Number.isInteger(e) && e % 4 === 1) {
    // .125 or .625: the hundredth below (2 and 6 are even); .375 and .875 already round up to 8
    const down = (Math.abs(x) - 0.005).toFixed(2);
    return x < 0 ? "-" + down : down;
  }
  return x.toFixed(2);
}
