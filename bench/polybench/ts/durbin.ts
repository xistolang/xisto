// PolyBench/C 4.2.1, linear-algebra/solvers/durbin (durbin.c, durbin.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (durbin.h); LARGE unless the command line names another
const N: int[] = [40, 120, 400, 2000, 4000];

function init_array(n: int, r: number[]): void {
  for (let i = 0; i < n; i++) {
    r[i] = n + 1 - i;
  }
}

function print_array(n: int, y: number[]): void {
  dumpStart();
  dumpBegin("y");
  for (let i = 0; i < n; i++) {
    if (i % 20 === 0) dumpWrite("\n");
    dumpWrite(fmt2(y[i]) + " ");
  }
  dumpEnd("y");
  dumpFinish();
}

function kernel_durbin(n: int, r: number[], y: number[]): void {
  const z: number[] = new Array<number>(n).fill(0);
  let alpha: number;
  let beta: number;
  let sum: number;

  y[0] = -r[0];
  beta = 1.0;
  alpha = -r[0];

  for (let k = 1; k < n; k++) {
    beta = (1 - alpha * alpha) * beta;
    sum = 0.0;
    for (let i = 0; i < k; i++) {
      sum += r[k - i - 1] * y[i];
    }
    alpha = -(r[k] + sum) / beta;

    for (let i = 0; i < k; i++) {
      z[i] = y[i] + alpha * y[k - i - 1];
    }
    for (let i = 0; i < k; i++) {
      y[i] = z[i];
    }
    y[k] = alpha;
  }
}

function main(): void {
  const n = N[dataset()];

  const r: number[] = new Array<number>(n).fill(0);
  const y: number[] = new Array<number>(n).fill(0);

  init_array(n, r);

  timerStart();
  kernel_durbin(n, r, y);
  timerStop();
  timerPrint();

  print_array(n, y);
}

main();
