// PolyBench/C 4.2.1, linear-algebra/solvers/ludcmp (ludcmp.c, ludcmp.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpArrays, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (ludcmp.h); LARGE unless the command line names another
const N: int[] = [40, 120, 400, 2000, 4000];

function init_array(n: int, A: number[][], b: number[], x: number[], y: number[]): void {
  const fn: number = n;
  for (let i = 0; i < n; i++) {
    x[i] = 0;
    y[i] = 0;
    b[i] = (i + 1) / fn / 2.0 + 4;
  }
  for (let i = 0; i < n; i++) {
    for (let j = 0; j <= i; j++)
      A[i][j] = (-j % n) / n + 1;
    for (let j = i + 1; j < n; j++) {
      A[i][j] = 0;
    }
    A[i][i] = 1;
  }
  // Make the matrix positive semi-definite.
  // not necessary for LU, but using same code as cholesky
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

function print_array(n: int, x: number[]): void {
  dumpStart();
  dumpBegin("x");
  for (let i = 0; i < n; i++) {
    if (i % 20 === 0) dumpWrite("\n");
    dumpWrite(fmt2(x[i]) + " ");
  }
  dumpEnd("x");
  dumpFinish();
}

function kernel_ludcmp(n: int, A: number[][], b: number[], x: number[], y: number[]): void {
  let w: number;
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < i; j++) {
      w = A[i][j];
      for (let k = 0; k < j; k++) {
        w -= A[i][k] * A[k][j];
      }
      A[i][j] = w / A[j][j];
    }
    for (let j = i; j < n; j++) {
      w = A[i][j];
      for (let k = 0; k < i; k++) {
        w -= A[i][k] * A[k][j];
      }
      A[i][j] = w;
    }
  }
  for (let i = 0; i < n; i++) {
    w = b[i];
    for (let j = 0; j < i; j++)
      w -= A[i][j] * y[j];
    y[i] = w;
  }
  for (let i = n - 1; i >= 0; i--) {
    w = y[i];
    for (let j = i + 1; j < n; j++)
      w -= A[i][j] * x[j];
    x[i] = w / A[i][i];
  }
}

function main(): void {
  const n = N[dataset()];

  const A: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const b: number[] = new Array<number>(n).fill(0);
  const x: number[] = new Array<number>(n).fill(0);
  const y: number[] = new Array<number>(n).fill(0);

  init_array(n, A, b, x, y);

  timerStart();
  kernel_ludcmp(n, A, b, x, y);
  timerStop();
  timerPrint();

  if (dumpArrays()) print_array(n, x);
}

main();
