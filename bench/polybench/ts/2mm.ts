// PolyBench/C 4.2.1, linear-algebra/kernels/2mm (2mm.c, 2mm.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (2mm.h); LARGE unless the command line names another
const NI: int[] = [16, 40, 180, 800, 1600];
const NJ: int[] = [18, 50, 190, 900, 1800];
const NK: int[] = [22, 70, 210, 1100, 2200];
const NL: int[] = [24, 80, 220, 1200, 2400];

function init_array(ni: int, nj: int, nk: int, nl: int, A: number[][], B: number[][], C: number[][], D: number[][]): void {
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nk; j++)
      A[i][j] = ((i * j + 1) % ni) / ni;
  for (let i = 0; i < nk; i++)
    for (let j = 0; j < nj; j++)
      B[i][j] = (i * (j + 1) % nj) / nj;
  for (let i = 0; i < nj; i++)
    for (let j = 0; j < nl; j++)
      C[i][j] = ((i * (j + 3) + 1) % nl) / nl;
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nl; j++)
      D[i][j] = (i * (j + 2) % nk) / nk;
}

function print_array(ni: int, nl: int, D: number[][]): void {
  dumpStart();
  dumpBegin("D");
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nl; j++) {
      if ((i * ni + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(D[i][j]) + " ");
    }
  dumpEnd("D");
  dumpFinish();
}

function kernel_2mm(ni: int, nj: int, nk: int, nl: int, alpha: number, beta: number, tmp: number[][], A: number[][],
                    B: number[][], C: number[][], D: number[][]): void {
  // D := alpha*A*B*C + beta*D
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nj; j++) {
      tmp[i][j] = 0.0;
      for (let k = 0; k < nk; ++k)
        tmp[i][j] += alpha * A[i][k] * B[k][j];
    }
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nl; j++) {
      D[i][j] *= beta;
      for (let k = 0; k < nj; ++k)
        D[i][j] += tmp[i][k] * C[k][j];
    }
}

function main(): void {
  const d = dataset();
  const ni = NI[d], nj = NJ[d], nk = NK[d], nl = NL[d];

  const alpha = 1.5;
  const beta = 1.2;
  const tmp: number[][] = Array.from({ length: ni }, () => new Array<number>(nj).fill(0));
  const A: number[][] = Array.from({ length: ni }, () => new Array<number>(nk).fill(0));
  const B: number[][] = Array.from({ length: nk }, () => new Array<number>(nj).fill(0));
  const C: number[][] = Array.from({ length: nj }, () => new Array<number>(nl).fill(0));
  const D: number[][] = Array.from({ length: ni }, () => new Array<number>(nl).fill(0));

  init_array(ni, nj, nk, nl, A, B, C, D);

  timerStart();
  kernel_2mm(ni, nj, nk, nl, alpha, beta, tmp, A, B, C, D);
  timerStop();
  timerPrint();

  print_array(ni, nl, D);
}

main();
