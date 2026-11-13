// PolyBench/C 4.2.1, linear-algebra/kernels/mvt (mvt.c, mvt.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (mvt.h); LARGE unless the command line names another
const N: int[] = [40, 120, 400, 2000, 4000];

function init_array(n: int, x1: number[], x2: number[], y_1: number[], y_2: number[], A: number[][]): void {
  for (let i = 0; i < n; i++) {
    x1[i] = (i % n) / n;
    x2[i] = ((i + 1) % n) / n;
    y_1[i] = ((i + 3) % n) / n;
    y_2[i] = ((i + 4) % n) / n;
    for (let j = 0; j < n; j++)
      A[i][j] = (i * j % n) / n;
  }
}

function print_array(n: int, x1: number[], x2: number[]): void {
  dumpStart();
  dumpBegin("x1");
  for (let i = 0; i < n; i++) {
    if (i % 20 === 0) dumpWrite("\n");
    dumpWrite(fmt2(x1[i]) + " ");
  }
  dumpEnd("x1");
  dumpBegin("x2");
  for (let i = 0; i < n; i++) {
    if (i % 20 === 0) dumpWrite("\n");
    dumpWrite(fmt2(x2[i]) + " ");
  }
  dumpEnd("x2");
  dumpFinish();
}

function kernel_mvt(n: int, x1: number[], x2: number[], y_1: number[], y_2: number[], A: number[][]): void {
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      x1[i] = x1[i] + A[i][j] * y_1[j];
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      x2[i] = x2[i] + A[j][i] * y_2[j];
}

function main(): void {
  const d = dataset();
  const n = N[d];

  const A: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const x1: number[] = new Array<number>(n).fill(0);
  const x2: number[] = new Array<number>(n).fill(0);
  const y_1: number[] = new Array<number>(n).fill(0);
  const y_2: number[] = new Array<number>(n).fill(0);

  init_array(n, x1, x2, y_1, y_2, A);

  timerStart();
  kernel_mvt(n, x1, x2, y_1, y_2, A);
  timerStop();
  timerPrint();

  print_array(n, x1, x2);
}

main();
