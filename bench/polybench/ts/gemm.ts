// PolyBench/C 4.2.1, linear-algebra/blas/gemm (gemm.c, gemm.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpArrays, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (gemm.h); LARGE unless the command line names another
const NI: int[] = [20, 60, 200, 1000, 2000];
const NJ: int[] = [25, 70, 220, 1100, 2300];
const NK: int[] = [30, 80, 240, 1200, 2600];

function init_array(ni: int, nj: int, nk: int, C: number[][], A: number[][], B: number[][]): void {
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nj; j++)
      C[i][j] = ((i * j + 1) % ni) / ni;
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nk; j++)
      A[i][j] = (i * (j + 1) % nk) / nk;
  for (let i = 0; i < nk; i++)
    for (let j = 0; j < nj; j++)
      B[i][j] = (i * (j + 2) % nj) / nj;
}

function print_array(ni: int, nj: int, C: number[][]): void {
  dumpStart();
  dumpBegin("C");
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nj; j++) {
      if ((i * ni + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(C[i][j]) + " ");
    }
  dumpEnd("C");
  dumpFinish();
}

function kernel_gemm(ni: int, nj: int, nk: int, alpha: number, beta: number, C: number[][], A: number[][], B: number[][]): void {
  for (let i = 0; i < ni; i++) {
    for (let j = 0; j < nj; j++)
      C[i][j] *= beta;
    for (let k = 0; k < nk; k++) {
      for (let j = 0; j < nj; j++)
        C[i][j] += alpha * A[i][k] * B[k][j];
    }
  }
}

function main(): void {
  const d = dataset();
  const ni = NI[d], nj = NJ[d], nk = NK[d];

  const alpha = 1.5;
  const beta = 1.2;
  const C: number[][] = Array.from({ length: ni }, () => new Array<number>(nj).fill(0));
  const A: number[][] = Array.from({ length: ni }, () => new Array<number>(nk).fill(0));
  const B: number[][] = Array.from({ length: nk }, () => new Array<number>(nj).fill(0));

  init_array(ni, nj, nk, C, A, B);

  timerStart();
  kernel_gemm(ni, nj, nk, alpha, beta, C, A, B);
  timerStop();
  timerPrint();

  if (dumpArrays()) print_array(ni, nj, C);
}

main();
