// PolyBench/C 4.2.1, linear-algebra/blas/symm (symm.c, symm.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (symm.h); LARGE unless the command line names another
const M: int[] = [20, 60, 200, 1000, 2000];
const N: int[] = [30, 80, 240, 1200, 2600];

function init_array(m: int, n: int, C: number[][], A: number[][], B: number[][]): void {
  for (let i = 0; i < m; i++)
    for (let j = 0; j < n; j++) {
      C[i][j] = ((i + j) % 100) / m;
      B[i][j] = ((n + i - j) % 100) / m;
    }
  for (let i = 0; i < m; i++) {
    for (let j = 0; j <= i; j++)
      A[i][j] = ((i + j) % 100) / m;
    for (let j = i + 1; j < m; j++)
      A[i][j] = -999; // regions of arrays that should not be used
  }
}

function print_array(m: int, n: int, C: number[][]): void {
  dumpStart();
  dumpBegin("C");
  for (let i = 0; i < m; i++)
    for (let j = 0; j < n; j++) {
      if ((i * m + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(C[i][j]) + " ");
    }
  dumpEnd("C");
  dumpFinish();
}

function kernel_symm(m: int, n: int, alpha: number, beta: number, C: number[][], A: number[][], B: number[][]): void {
  let temp2: number;
  for (let i = 0; i < m; i++)
    for (let j = 0; j < n; j++) {
      temp2 = 0;
      for (let k = 0; k < i; k++) {
        C[k][j] += alpha * B[i][j] * A[i][k];
        temp2 += B[k][j] * A[i][k];
      }
      C[i][j] = beta * C[i][j] + alpha * B[i][j] * A[i][i] + alpha * temp2;
    }
}

function main(): void {
  const d = dataset();
  const m = M[d], n = N[d];

  const alpha = 1.5;
  const beta = 1.2;
  const C: number[][] = Array.from({ length: m }, () => new Array<number>(n).fill(0));
  const A: number[][] = Array.from({ length: m }, () => new Array<number>(m).fill(0));
  const B: number[][] = Array.from({ length: m }, () => new Array<number>(n).fill(0));

  init_array(m, n, C, A, B);

  timerStart();
  kernel_symm(m, n, alpha, beta, C, A, B);
  timerStop();
  timerPrint();

  print_array(m, n, C);
}

main();
