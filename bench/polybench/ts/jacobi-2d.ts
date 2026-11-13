// PolyBench/C 4.2.1, stencils/jacobi-2d (jacobi-2d.c, jacobi-2d.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpArrays, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (jacobi-2d.h); LARGE unless the command line names another
const TSTEPS: int[] = [20, 40, 100, 500, 1000];
const N: int[] = [30, 90, 250, 1300, 2800];

function init_array(n: int, A: number[][], B: number[][]): void {
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      A[i][j] = (i * (j + 2) + 2) / n;
      B[i][j] = (i * (j + 3) + 3) / n;
    }
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

function kernel_jacobi_2d(tsteps: int, n: int, A: number[][], B: number[][]): void {
  for (let t = 0; t < tsteps; t++) {
    for (let i = 1; i < n - 1; i++)
      for (let j = 1; j < n - 1; j++)
        B[i][j] = 0.2 * (A[i][j] + A[i][j - 1] + A[i][1 + j] + A[1 + i][j] + A[i - 1][j]);
    for (let i = 1; i < n - 1; i++)
      for (let j = 1; j < n - 1; j++)
        A[i][j] = 0.2 * (B[i][j] + B[i][j - 1] + B[i][1 + j] + B[1 + i][j] + B[i - 1][j]);
  }
}

function main(): void {
  const d = dataset();
  const n = N[d];
  const tsteps = TSTEPS[d];

  const A: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const B: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));

  init_array(n, A, B);

  timerStart();
  kernel_jacobi_2d(tsteps, n, A, B);
  timerStop();
  timerPrint();

  if (dumpArrays()) print_array(n, A);
}

main();
