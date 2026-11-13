// PolyBench/C 4.2.1, datamining/covariance (covariance.c, covariance.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (covariance.h); LARGE unless the command line names another
const M: int[] = [28, 80, 240, 1200, 2600];
const N: int[] = [32, 100, 260, 1400, 3000];

// float_n comes back as the result (C: DATA_TYPE *float_n)
function init_array(m: int, n: int, data: number[][]): number {
  const float_n: number = n;
  for (let i = 0; i < n; i++)
    for (let j = 0; j < m; j++)
      data[i][j] = (i * j) / m;
  return float_n;
}

function print_array(m: int, cov: number[][]): void {
  dumpStart();
  dumpBegin("cov");
  for (let i = 0; i < m; i++)
    for (let j = 0; j < m; j++) {
      if ((i * m + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(cov[i][j]) + " ");
    }
  dumpEnd("cov");
  dumpFinish();
}

function kernel_covariance(m: int, n: int, float_n: number, data: number[][], cov: number[][], mean: number[]): void {
  for (let j = 0; j < m; j++) {
    mean[j] = 0.0;
    for (let i = 0; i < n; i++)
      mean[j] += data[i][j];
    mean[j] /= float_n;
  }

  for (let i = 0; i < n; i++)
    for (let j = 0; j < m; j++)
      data[i][j] -= mean[j];

  for (let i = 0; i < m; i++)
    for (let j = i; j < m; j++) {
      cov[i][j] = 0.0;
      for (let k = 0; k < n; k++)
        cov[i][j] += data[k][i] * data[k][j];
      cov[i][j] /= (float_n - 1.0);
      cov[j][i] = cov[i][j];
    }
}

function main(): void {
  const d = dataset();
  const n = N[d], m = M[d];

  const data: number[][] = Array.from({ length: n }, () => new Array<number>(m).fill(0));
  const cov: number[][] = Array.from({ length: m }, () => new Array<number>(m).fill(0));
  const mean: number[] = new Array<number>(m).fill(0);

  const float_n = init_array(m, n, data);

  timerStart();
  kernel_covariance(m, n, float_n, data, cov, mean);
  timerStop();
  timerPrint();

  print_array(m, cov);
}

main();
