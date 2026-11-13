// PolyBench/C 4.2.1, linear-algebra/blas/gemver (gemver.c, gemver.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (gemver.h); LARGE unless the command line names another
const N: int[] = [40, 120, 400, 2000, 4000];

function init_array(n: int, A: number[][], u1: number[], v1: number[], u2: number[], v2: number[],
                    w: number[], x: number[], y: number[], z: number[]): void {
  const fn: number = n;
  for (let i = 0; i < n; i++) {
    u1[i] = i;
    u2[i] = ((i + 1) / fn) / 2.0;
    v1[i] = ((i + 1) / fn) / 4.0;
    v2[i] = ((i + 1) / fn) / 6.0;
    y[i] = ((i + 1) / fn) / 8.0;
    z[i] = ((i + 1) / fn) / 9.0;
    x[i] = 0.0;
    w[i] = 0.0;
    for (let j = 0; j < n; j++)
      A[i][j] = (i * j % n) / n;
  }
}

function print_array(n: int, w: number[]): void {
  dumpStart();
  dumpBegin("w");
  for (let i = 0; i < n; i++) {
    if (i % 20 === 0) dumpWrite("\n");
    dumpWrite(fmt2(w[i]) + " ");
  }
  dumpEnd("w");
  dumpFinish();
}

function kernel_gemver(n: int, alpha: number, beta: number, A: number[][], u1: number[], v1: number[], u2: number[],
                       v2: number[], w: number[], x: number[], y: number[], z: number[]): void {
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      A[i][j] = A[i][j] + u1[i] * v1[j] + u2[i] * v2[j];
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      x[i] = x[i] + beta * A[j][i] * y[j];
  for (let i = 0; i < n; i++)
    x[i] = x[i] + z[i];
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      w[i] = w[i] + alpha * A[i][j] * x[j];
}

function main(): void {
  const d = dataset();
  const n = N[d];

  const alpha = 1.5;
  const beta = 1.2;
  const A: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const u1: number[] = new Array<number>(n).fill(0);
  const v1: number[] = new Array<number>(n).fill(0);
  const u2: number[] = new Array<number>(n).fill(0);
  const v2: number[] = new Array<number>(n).fill(0);
  const w: number[] = new Array<number>(n).fill(0);
  const x: number[] = new Array<number>(n).fill(0);
  const y: number[] = new Array<number>(n).fill(0);
  const z: number[] = new Array<number>(n).fill(0);

  init_array(n, A, u1, v1, u2, v2, w, x, y, z);

  timerStart();
  kernel_gemver(n, alpha, beta, A, u1, v1, u2, v2, w, x, y, z);
  timerStop();
  timerPrint();

  print_array(n, w);
}

main();
