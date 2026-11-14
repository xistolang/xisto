// PolyBench/C 4.2.1, linear-algebra/solvers/trisolv (trisolv.c, trisolv.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpArrays, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (trisolv.h); LARGE unless the command line names another
const N: int[] = [40, 120, 400, 2000, 4000];

function init_array(n: int, L: number[][], x: number[], b: number[]): void {
  for (let i = 0; i < n; i++) {
    x[i] = -999;
    b[i] = i;
    for (let j = 0; j <= i; j++)
      L[i][j] = (i + n - j + 1) * 2 / n;
  }
}

function print_array(n: int, x: number[]): void {
  dumpStart();
  dumpBegin("x");
  for (let i = 0; i < n; i++) {
    dumpWrite(fmt2(x[i]) + " ");
    if (i % 20 === 0) dumpWrite("\n");
  }
  dumpEnd("x");
  dumpFinish();
}

function kernel_trisolv(n: int, L: number[][], x: number[], b: number[]): void {
  for (let i = 0; i < n; i++) {
    x[i] = b[i];
    for (let j = 0; j < i; j++)
      x[i] -= L[i][j] * x[j];
    x[i] = x[i] / L[i][i];
  }
}

function main(): void {
  const n = N[dataset()];

  const L: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const x: number[] = new Array<number>(n).fill(0);
  const b: number[] = new Array<number>(n).fill(0);

  init_array(n, L, x, b);

  timerStart();
  kernel_trisolv(n, L, x, b);
  timerStop();
  timerPrint();

  if (dumpArrays()) print_array(n, x);
}

main();
