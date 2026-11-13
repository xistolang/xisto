// PolyBench/C 4.2.1, stencils/jacobi-1d (jacobi-1d.c, jacobi-1d.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (jacobi-1d.h); LARGE unless the command line names another
const TSTEPS: int[] = [20, 40, 100, 500, 1000];
const N: int[] = [30, 120, 400, 2000, 4000];

function init_array(n: int, A: number[], B: number[]): void {
  for (let i = 0; i < n; i++) {
    A[i] = (i + 2) / n;
    B[i] = (i + 3) / n;
  }
}

function print_array(n: int, A: number[]): void {
  dumpStart();
  dumpBegin("A");
  for (let i = 0; i < n; i++) {
    if (i % 20 === 0) dumpWrite("\n");
    dumpWrite(fmt2(A[i]) + " ");
  }
  dumpEnd("A");
  dumpFinish();
}

function kernel_jacobi_1d(tsteps: int, n: int, A: number[], B: number[]): void {
  for (let t = 0; t < tsteps; t++) {
    for (let i = 1; i < n - 1; i++)
      B[i] = 0.33333 * (A[i - 1] + A[i] + A[i + 1]);
    for (let i = 1; i < n - 1; i++)
      A[i] = 0.33333 * (B[i - 1] + B[i] + B[i + 1]);
  }
}

function main(): void {
  const d = dataset();
  const n = N[d];
  const tsteps = TSTEPS[d];

  const A: number[] = new Array<number>(n).fill(0);
  const B: number[] = new Array<number>(n).fill(0);

  init_array(n, A, B);

  timerStart();
  kernel_jacobi_1d(tsteps, n, A, B);
  timerStop();
  timerPrint();

  print_array(n, A);
}

main();
