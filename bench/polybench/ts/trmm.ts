// PolyBench/C 4.2.1, linear-algebra/blas/trmm (trmm.c, trmm.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (trmm.h); LARGE unless the command line names another
const M: int[] = [20, 60, 200, 1000, 2000];
const N: int[] = [30, 80, 240, 1200, 2600];

function init_array(m: int, n: int, A: number[][], B: number[][]): void {
  for (let i = 0; i < m; i++) {
    for (let j = 0; j < i; j++) {
      A[i][j] = ((i + j) % m) / m;
    }
    A[i][i] = 1.0;
    for (let j = 0; j < n; j++) {
      B[i][j] = ((n + (i - j)) % n) / n;
    }
  }
}

function print_array(m: int, n: int, B: number[][]): void {
  dumpStart();
  dumpBegin("B");
  for (let i = 0; i < m; i++)
    for (let j = 0; j < n; j++) {
      if ((i * m + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(B[i][j]) + " ");
    }
  dumpEnd("B");
  dumpFinish();
}

function kernel_trmm(m: int, n: int, alpha: number, A: number[][], B: number[][]): void {
  for (let i = 0; i < m; i++)
    for (let j = 0; j < n; j++) {
      for (let k = i + 1; k < m; k++)
        B[i][j] += A[k][i] * B[k][j];
      B[i][j] = alpha * B[i][j];
    }
}

function main(): void {
  const d = dataset();
  const m = M[d], n = N[d];

  const alpha = 1.5;
  const A: number[][] = Array.from({ length: m }, () => new Array<number>(m).fill(0));
  const B: number[][] = Array.from({ length: m }, () => new Array<number>(n).fill(0));

  init_array(m, n, A, B);

  timerStart();
  kernel_trmm(m, n, alpha, A, B);
  timerStop();
  timerPrint();

  print_array(m, n, B);
}

main();
