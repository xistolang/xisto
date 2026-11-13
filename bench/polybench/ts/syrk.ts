// PolyBench/C 4.2.1, linear-algebra/blas/syrk (syrk.c, syrk.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (syrk.h); LARGE unless the command line names another
const M: int[] = [20, 60, 200, 1000, 2000];
const N: int[] = [30, 80, 240, 1200, 2600];

function init_array(n: int, m: int, C: number[][], A: number[][]): void {
  for (let i = 0; i < n; i++)
    for (let j = 0; j < m; j++)
      A[i][j] = ((i * j + 1) % n) / n;
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      C[i][j] = ((i * j + 2) % m) / m;
}

function print_array(n: int, C: number[][]): void {
  dumpStart();
  dumpBegin("C");
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      if ((i * n + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(C[i][j]) + " ");
    }
  dumpEnd("C");
  dumpFinish();
}

function kernel_syrk(n: int, m: int, alpha: number, beta: number, C: number[][], A: number[][]): void {
  for (let i = 0; i < n; i++) {
    for (let j = 0; j <= i; j++)
      C[i][j] *= beta;
    for (let k = 0; k < m; k++) {
      for (let j = 0; j <= i; j++)
        C[i][j] += alpha * A[i][k] * A[j][k];
    }
  }
}

function main(): void {
  const d = dataset();
  const n = N[d], m = M[d];

  const alpha = 1.5;
  const beta = 1.2;
  const C: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const A: number[][] = Array.from({ length: n }, () => new Array<number>(m).fill(0));

  init_array(n, m, C, A);

  timerStart();
  kernel_syrk(n, m, alpha, beta, C, A);
  timerStop();
  timerPrint();

  print_array(n, C);
}

main();
