// PolyBench/C 4.2.1, stencils/heat-3d (heat-3d.c, heat-3d.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpArrays, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (heat-3d.h); LARGE unless the command line names another
const TSTEPS: int[] = [20, 40, 100, 500, 1000];
const N: int[] = [10, 20, 40, 120, 200];

function init_array(n: int, A: number[][][], B: number[][][]): void {
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      for (let k = 0; k < n; k++)
        A[i][j][k] = B[i][j][k] = (i + j + (n - k)) * 10 / n;
}

function print_array(n: int, A: number[][][]): void {
  dumpStart();
  dumpBegin("A");
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      for (let k = 0; k < n; k++) {
        if ((i * n * n + j * n + k) % 20 === 0) dumpWrite("\n");
        dumpWrite(fmt2(A[i][j][k]) + " ");
      }
  dumpEnd("A");
  dumpFinish();
}

function kernel_heat_3d(tsteps: int, n: int, A: number[][][], B: number[][][]): void {
  for (let t = 1; t <= tsteps; t++) {
    for (let i = 1; i < n - 1; i++) {
      for (let j = 1; j < n - 1; j++) {
        for (let k = 1; k < n - 1; k++) {
          B[i][j][k] = 0.125 * (A[i + 1][j][k] - 2.0 * A[i][j][k] + A[i - 1][j][k])
                     + 0.125 * (A[i][j + 1][k] - 2.0 * A[i][j][k] + A[i][j - 1][k])
                     + 0.125 * (A[i][j][k + 1] - 2.0 * A[i][j][k] + A[i][j][k - 1])
                     + A[i][j][k];
        }
      }
    }
    for (let i = 1; i < n - 1; i++) {
      for (let j = 1; j < n - 1; j++) {
        for (let k = 1; k < n - 1; k++) {
          A[i][j][k] = 0.125 * (B[i + 1][j][k] - 2.0 * B[i][j][k] + B[i - 1][j][k])
                     + 0.125 * (B[i][j + 1][k] - 2.0 * B[i][j][k] + B[i][j - 1][k])
                     + 0.125 * (B[i][j][k + 1] - 2.0 * B[i][j][k] + B[i][j][k - 1])
                     + B[i][j][k];
        }
      }
    }
  }
}

function main(): void {
  const d = dataset();
  const n = N[d];
  const tsteps = TSTEPS[d];

  const A: number[][][] = Array.from({ length: n }, () => Array.from({ length: n }, () => new Array<number>(n).fill(0)));
  const B: number[][][] = Array.from({ length: n }, () => Array.from({ length: n }, () => new Array<number>(n).fill(0)));

  init_array(n, A, B);

  timerStart();
  kernel_heat_3d(tsteps, n, A, B);
  timerStop();
  timerPrint();

  if (dumpArrays()) print_array(n, A);
}

main();
