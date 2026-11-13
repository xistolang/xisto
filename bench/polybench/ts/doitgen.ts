// PolyBench/C 4.2.1, linear-algebra/kernels/doitgen (doitgen.c, doitgen.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (doitgen.h); LARGE unless the command line names another
const NQ: int[] = [8, 20, 40, 140, 220];
const NR: int[] = [10, 25, 50, 150, 250];
const NP: int[] = [12, 30, 60, 160, 270];

function init_array(nr: int, nq: int, np: int, A: number[][][], C4: number[][]): void {
  for (let i = 0; i < nr; i++)
    for (let j = 0; j < nq; j++)
      for (let k = 0; k < np; k++)
        A[i][j][k] = ((i * j + k) % np) / np;
  for (let i = 0; i < np; i++)
    for (let j = 0; j < np; j++)
      C4[i][j] = (i * j % np) / np;
}

function print_array(nr: int, nq: int, np: int, A: number[][][]): void {
  dumpStart();
  dumpBegin("A");
  for (let i = 0; i < nr; i++)
    for (let j = 0; j < nq; j++)
      for (let k = 0; k < np; k++) {
        if ((i * nq * np + j * np + k) % 20 === 0) dumpWrite("\n");
        dumpWrite(fmt2(A[i][j][k]) + " ");
      }
  dumpEnd("A");
  dumpFinish();
}

function kernel_doitgen(nr: int, nq: int, np: int, A: number[][][], C4: number[][], sum: number[]): void {
  for (let r = 0; r < nr; r++)
    for (let q = 0; q < nq; q++) {
      for (let p = 0; p < np; p++) {
        sum[p] = 0.0;
        for (let s = 0; s < np; s++)
          sum[p] += A[r][q][s] * C4[s][p];
      }
      for (let p = 0; p < np; p++)
        A[r][q][p] = sum[p];
    }
}

function main(): void {
  const d = dataset();
  const nr = NR[d], nq = NQ[d], np = NP[d];

  const A: number[][][] = Array.from({ length: nr }, () => Array.from({ length: nq }, () => new Array<number>(np).fill(0)));
  const sum: number[] = new Array<number>(np).fill(0);
  const C4: number[][] = Array.from({ length: np }, () => new Array<number>(np).fill(0));

  init_array(nr, nq, np, A, C4);

  timerStart();
  kernel_doitgen(nr, nq, np, A, C4, sum);
  timerStop();
  timerPrint();

  print_array(nr, nq, np, A);
}

main();
