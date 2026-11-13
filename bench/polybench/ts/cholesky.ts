// PolyBench/C 4.2.1, linear-algebra/solvers/cholesky (cholesky.c, cholesky.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (cholesky.h); LARGE unless the command line names another
const N: int[] = [40, 120, 400, 2000, 4000];

function init_array(n: int, A: number[][]): void {
  for (let i = 0; i < n; i++) {
    for (let j = 0; j <= i; j++)
      A[i][j] = (-j % n) / n + 1;
    for (let j = i + 1; j < n; j++) {
      A[i][j] = 0;
    }
    A[i][i] = 1;
  }
  // Make the matrix positive semi-definite.
  const B: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  for (let r = 0; r < n; ++r)
    for (let s = 0; s < n; ++s)
      B[r][s] = 0;
  for (let t = 0; t < n; ++t)
    for (let r = 0; r < n; ++r)
      for (let s = 0; s < n; ++s)
        B[r][s] += A[r][t] * A[s][t];
  for (let r = 0; r < n; ++r)
    for (let s = 0; s < n; ++s)
      A[r][s] = B[r][s];
}

function print_array(n: int, A: number[][]): void {
  dumpStart();
  dumpBegin("A");
  for (let i = 0; i < n; i++)
    for (let j = 0; j <= i; j++) {
      if ((i * n + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(A[i][j]) + " ");
    }
  dumpEnd("A");
  dumpFinish();
}

function kernel_cholesky(n: int, A: number[][]): void {
  for (let i = 0; i < n; i++) {
    // j<i
    for (let j = 0; j < i; j++) {
      for (let k = 0; k < j; k++) {
        A[i][j] -= A[i][k] * A[j][k];
      }
      A[i][j] /= A[j][j];
    }
    // i==j case
    for (let k = 0; k < i; k++) {
      A[i][i] -= A[i][k] * A[i][k];
    }
    A[i][i] = Math.sqrt(A[i][i]);
  }
}

function main(): void {
  const n = N[dataset()];

  const A: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));

  init_array(n, A);

  timerStart();
  kernel_cholesky(n, A);
  timerStop();
  timerPrint();

  print_array(n, A);
}

main();
