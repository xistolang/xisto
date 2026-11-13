// PolyBench/C 4.2.1, linear-algebra/kernels/atax (atax.c, atax.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (atax.h); LARGE unless the command line names another
const M: int[] = [38, 116, 390, 1900, 1800];
const N: int[] = [42, 124, 410, 2100, 2200];

function init_array(m: int, n: int, A: number[][], x: number[]): void {
  const fn: number = n;
  for (let i = 0; i < n; i++)
    x[i] = 1 + (i / fn);
  for (let i = 0; i < m; i++)
    for (let j = 0; j < n; j++)
      A[i][j] = ((i + j) % n) / (5 * m);
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

function kernel_atax(m: int, n: int, A: number[][], x: number[], y: number[], tmp: number[]): void {
  for (let i = 0; i < n; i++)
    y[i] = 0;
  for (let i = 0; i < m; i++) {
    tmp[i] = 0.0;
    for (let j = 0; j < n; j++)
      tmp[i] = tmp[i] + A[i][j] * x[j];
    for (let j = 0; j < n; j++)
      y[j] = y[j] + A[i][j] * tmp[i];
  }
}

function main(): void {
  const d = dataset();
  const m = M[d], n = N[d];

  const A: number[][] = Array.from({ length: m }, () => new Array<number>(n).fill(0));
  const x: number[] = new Array<number>(n).fill(0);
  const y: number[] = new Array<number>(n).fill(0);
  const tmp: number[] = new Array<number>(m).fill(0);

  init_array(m, n, A, x);

  timerStart();
  kernel_atax(m, n, A, x, y, tmp);
  timerStop();
  timerPrint();

  print_array(n, y);
}

main();
