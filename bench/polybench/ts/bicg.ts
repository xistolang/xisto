// PolyBench/C 4.2.1, linear-algebra/kernels/bicg (bicg.c, bicg.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpArrays, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (bicg.h); LARGE unless the command line names another
const M: int[] = [38, 116, 390, 1900, 1800];
const N: int[] = [42, 124, 410, 2100, 2200];

function init_array(m: int, n: int, A: number[][], r: number[], p: number[]): void {
  for (let i = 0; i < m; i++)
    p[i] = (i % m) / m;
  for (let i = 0; i < n; i++) {
    r[i] = (i % n) / n;
    for (let j = 0; j < m; j++)
      A[i][j] = (i * (j + 1) % n) / n;
  }
}

function print_array(m: int, n: int, s: number[], q: number[]): void {
  dumpStart();
  dumpBegin("s");
  for (let i = 0; i < m; i++) {
    if (i % 20 === 0) dumpWrite("\n");
    dumpWrite(fmt2(s[i]) + " ");
  }
  dumpEnd("s");
  dumpBegin("q");
  for (let i = 0; i < n; i++) {
    if (i % 20 === 0) dumpWrite("\n");
    dumpWrite(fmt2(q[i]) + " ");
  }
  dumpEnd("q");
  dumpFinish();
}

function kernel_bicg(m: int, n: int, A: number[][], s: number[], q: number[], p: number[], r: number[]): void {
  for (let i = 0; i < m; i++)
    s[i] = 0;
  for (let i = 0; i < n; i++) {
    q[i] = 0.0;
    for (let j = 0; j < m; j++) {
      s[j] = s[j] + r[i] * A[i][j];
      q[i] = q[i] + A[i][j] * p[j];
    }
  }
}

function main(): void {
  const d = dataset();
  const n = N[d], m = M[d];

  const A: number[][] = Array.from({ length: n }, () => new Array<number>(m).fill(0));
  const s: number[] = new Array<number>(m).fill(0);
  const q: number[] = new Array<number>(n).fill(0);
  const p: number[] = new Array<number>(m).fill(0);
  const r: number[] = new Array<number>(n).fill(0);

  init_array(m, n, A, r, p);

  timerStart();
  kernel_bicg(m, n, A, s, q, p, r);
  timerStop();
  timerPrint();

  if (dumpArrays()) print_array(m, n, s, q);
}

main();
