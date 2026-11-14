// PolyBench/C 4.2.1, medley/floyd-warshall (floyd-warshall.c, floyd-warshall.h), with types: the same loops in the
// same order, int as in C (DATA_TYPE_IS_INT): each row an Int32Array, 32-bit ints as C's.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpArrays, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (floyd-warshall.h); LARGE unless the command line names another
const N: int[] = [60, 180, 500, 2800, 5600];

function init_array(n: int, path: Int32Array[]): void {
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      path[i][j] = i * j % 7 + 1;
      if ((i + j) % 13 === 0 || (i + j) % 7 === 0 || (i + j) % 11 === 0)
        path[i][j] = 999;
    }
}

function print_array(n: int, path: Int32Array[]): void {
  dumpStart();
  dumpBegin("path");
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      if ((i * n + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(`${path[i][j]} `);
    }
  dumpEnd("path");
  dumpFinish();
}

function kernel_floyd_warshall(n: int, path: Int32Array[]): void {
  for (let k = 0; k < n; k++) {
    for (let i = 0; i < n; i++)
      for (let j = 0; j < n; j++)
        path[i][j] = path[i][j] < path[i][k] + path[k][j] ?
          path[i][j] : path[i][k] + path[k][j];
  }
}

function main(): void {
  const n = N[dataset()];

  const path: Int32Array[] = Array.from({ length: n }, () => new Int32Array(n));

  init_array(n, path);

  timerStart();
  kernel_floyd_warshall(n, path);
  timerStop();
  timerPrint();

  if (dumpArrays()) print_array(n, path);
}

main();
