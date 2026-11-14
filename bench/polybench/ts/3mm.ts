// PolyBench/C 4.2.1, linear-algebra/kernels/3mm (3mm.c, 3mm.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpArrays, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (3mm.h); LARGE unless the command line names another
const NI: int[] = [16, 40, 180, 800, 1600];
const NJ: int[] = [18, 50, 190, 900, 1800];
const NK: int[] = [20, 60, 200, 1000, 2000];
const NL: int[] = [22, 70, 210, 1100, 2200];
const NM: int[] = [24, 80, 220, 1200, 2400];

function init_array(ni: int, nj: int, nk: int, nl: int, nm: int, A: number[][], B: number[][], C: number[][],
                    D: number[][]): void {
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nk; j++)
      A[i][j] = ((i * j + 1) % ni) / (5 * ni);
  for (let i = 0; i < nk; i++)
    for (let j = 0; j < nj; j++)
      B[i][j] = ((i * (j + 1) + 2) % nj) / (5 * nj);
  for (let i = 0; i < nj; i++)
    for (let j = 0; j < nm; j++)
      C[i][j] = (i * (j + 3) % nl) / (5 * nl);
  for (let i = 0; i < nm; i++)
    for (let j = 0; j < nl; j++)
      D[i][j] = ((i * (j + 2) + 2) % nk) / (5 * nk);
}

function print_array(ni: int, nl: int, G: number[][]): void {
  dumpStart();
  dumpBegin("G");
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nl; j++) {
      if ((i * ni + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(G[i][j]) + " ");
    }
  dumpEnd("G");
  dumpFinish();
}

function kernel_3mm(ni: int, nj: int, nk: int, nl: int, nm: int, E: number[][], A: number[][], B: number[][],
                    F: number[][], C: number[][], D: number[][], G: number[][]): void {
  // E := A*B
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nj; j++) {
      E[i][j] = 0.0;
      for (let k = 0; k < nk; ++k)
        E[i][j] += A[i][k] * B[k][j];
    }
  // F := C*D
  for (let i = 0; i < nj; i++)
    for (let j = 0; j < nl; j++) {
      F[i][j] = 0.0;
      for (let k = 0; k < nm; ++k)
        F[i][j] += C[i][k] * D[k][j];
    }
  // G := E*F
  for (let i = 0; i < ni; i++)
    for (let j = 0; j < nl; j++) {
      G[i][j] = 0.0;
      for (let k = 0; k < nj; ++k)
        G[i][j] += E[i][k] * F[k][j];
    }
}

function main(): void {
  const d = dataset();
  const ni = NI[d], nj = NJ[d], nk = NK[d], nl = NL[d], nm = NM[d];

  const E: number[][] = Array.from({ length: ni }, () => new Array<number>(nj).fill(0));
  const A: number[][] = Array.from({ length: ni }, () => new Array<number>(nk).fill(0));
  const B: number[][] = Array.from({ length: nk }, () => new Array<number>(nj).fill(0));
  const F: number[][] = Array.from({ length: nj }, () => new Array<number>(nl).fill(0));
  const C: number[][] = Array.from({ length: nj }, () => new Array<number>(nm).fill(0));
  const D: number[][] = Array.from({ length: nm }, () => new Array<number>(nl).fill(0));
  const G: number[][] = Array.from({ length: ni }, () => new Array<number>(nl).fill(0));

  init_array(ni, nj, nk, nl, nm, A, B, C, D);

  timerStart();
  kernel_3mm(ni, nj, nk, nl, nm, E, A, B, F, C, D, G);
  timerStop();
  timerPrint();

  if (dumpArrays()) print_array(ni, nl, G);
}

main();
