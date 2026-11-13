// PolyBench/C 4.2.1, stencils/seidel-2d (seidel-2d.c, seidel-2d.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (seidel-2d.h); LARGE unless the command line names another
const TSTEPS: int[] = [20, 40, 100, 500, 1000];
const N: int[] = [40, 120, 400, 2000, 4000];

function init_array(n: int, A: number[][]): void {
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      A[i][j] = (i * (j + 2) + 2) / n;
}

function print_array(n: int, A: number[][]): void {
  dumpStart();
  dumpBegin("A");
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      if ((i * n + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(A[i][j]) + " ");
    }
  dumpEnd("A");
  dumpFinish();
}

function kernel_seidel_2d(tsteps: int, n: int, A: number[][]): void {
  for (let t = 0; t <= tsteps - 1; t++)
    for (let i = 1; i <= n - 2; i++)
      for (let j = 1; j <= n - 2; j++)
        A[i][j] = (A[i - 1][j - 1] + A[i - 1][j] + A[i - 1][j + 1]
                 + A[i][j - 1] + A[i][j] + A[i][j + 1]
                 + A[i + 1][j - 1] + A[i + 1][j] + A[i + 1][j + 1]) / 9.0;
}

function main(): void {
  const d = dataset();
  const n = N[d];
  const tsteps = TSTEPS[d];

  const A: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));

  init_array(n, A);

  timerStart();
  kernel_seidel_2d(tsteps, n, A);
  timerStop();
  timerPrint();

  print_array(n, A);
}

main();
