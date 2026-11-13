// PolyBench/C 4.2.1, linear-algebra/solvers/gramschmidt (gramschmidt.c, gramschmidt.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (gramschmidt.h); LARGE unless the command line names another
const M: int[] = [20, 60, 200, 1000, 2000];
const N: int[] = [30, 80, 240, 1200, 2600];

function init_array(m: int, n: int, A: number[][], R: number[][], Q: number[][]): void {
  for (let i = 0; i < m; i++)
    for (let j = 0; j < n; j++) {
      A[i][j] = (((i * j) % m) / m * 100) + 10;
      Q[i][j] = 0.0;
    }
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      R[i][j] = 0.0;
}

function print_array(m: int, n: int, A: number[][], R: number[][], Q: number[][]): void {
  dumpStart();
  dumpBegin("R");
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      if ((i * n + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(R[i][j]) + " ");
    }
  dumpEnd("R");

  dumpBegin("Q");
  for (let i = 0; i < m; i++)
    for (let j = 0; j < n; j++) {
      if ((i * n + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(Q[i][j]) + " ");
    }
  dumpEnd("Q");
  dumpFinish();
}

// QR Decomposition with Modified Gram Schmidt: http://www.inf.ethz.ch/personal/gander/
function kernel_gramschmidt(m: int, n: int, A: number[][], R: number[][], Q: number[][]): void {
  let nrm: number;
  for (let k = 0; k < n; k++) {
    nrm = 0.0;
    for (let i = 0; i < m; i++)
      nrm += A[i][k] * A[i][k];
    R[k][k] = Math.sqrt(nrm);
    for (let i = 0; i < m; i++)
      Q[i][k] = A[i][k] / R[k][k];
    for (let j = k + 1; j < n; j++) {
      R[k][j] = 0.0;
      for (let i = 0; i < m; i++)
        R[k][j] += Q[i][k] * A[i][j];
      for (let i = 0; i < m; i++)
        A[i][j] = A[i][j] - Q[i][k] * R[k][j];
    }
  }
}

function main(): void {
  const d = dataset();
  const m = M[d], n = N[d];

  const A: number[][] = Array.from({ length: m }, () => new Array<number>(n).fill(0));
  const R: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const Q: number[][] = Array.from({ length: m }, () => new Array<number>(n).fill(0));

  init_array(m, n, A, R, Q);

  timerStart();
  kernel_gramschmidt(m, n, A, R, Q);
  timerStop();
  timerPrint();

  print_array(m, n, A, R, Q);
}

main();
