// PolyBench/C 4.2.1, linear-algebra/blas/gesummv (gesummv.c, gesummv.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (gesummv.h); LARGE unless the command line names another
const N: int[] = [30, 90, 250, 1300, 2800];

function init_array(n: int, A: number[][], B: number[][], x: number[]): void {
  for (let i = 0; i < n; i++) {
    x[i] = (i % n) / n;
    for (let j = 0; j < n; j++) {
      A[i][j] = ((i * j + 1) % n) / n;
      B[i][j] = ((i * j + 2) % n) / n;
    }
  }
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

function kernel_gesummv(n: int, alpha: number, beta: number, A: number[][], B: number[][], tmp: number[], x: number[],
                        y: number[]): void {
  for (let i = 0; i < n; i++) {
    tmp[i] = 0.0;
    y[i] = 0.0;
    for (let j = 0; j < n; j++) {
      tmp[i] = A[i][j] * x[j] + tmp[i];
      y[i] = B[i][j] * x[j] + y[i];
    }
    y[i] = alpha * tmp[i] + beta * y[i];
  }
}

function main(): void {
  const d = dataset();
  const n = N[d];

  const alpha = 1.5;
  const beta = 1.2;
  const A: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const B: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const tmp: number[] = new Array<number>(n).fill(0);
  const x: number[] = new Array<number>(n).fill(0);
  const y: number[] = new Array<number>(n).fill(0);

  init_array(n, A, B, x);

  timerStart();
  kernel_gesummv(n, alpha, beta, A, B, tmp, x, y);
  timerStop();
  timerPrint();

  print_array(n, y);
}

main();
