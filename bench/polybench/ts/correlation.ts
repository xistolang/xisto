// PolyBench/C 4.2.1, datamining/correlation (correlation.c, correlation.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (correlation.h); LARGE unless the command line names another
const M: int[] = [28, 80, 240, 1200, 2600];
const N: int[] = [32, 100, 260, 1400, 3000];

// float_n comes back as the result (C: DATA_TYPE *float_n)
function init_array(m: int, n: int, data: number[][]): number {
  const float_n: number = n;
  for (let i = 0; i < n; i++)
    for (let j = 0; j < m; j++)
      data[i][j] = (i * j) / m + i;
  return float_n;
}

function print_array(m: int, corr: number[][]): void {
  dumpStart();
  dumpBegin("corr");
  for (let i = 0; i < m; i++)
    for (let j = 0; j < m; j++) {
      if ((i * m + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(corr[i][j]) + " ");
    }
  dumpEnd("corr");
  dumpFinish();
}

function kernel_correlation(m: int, n: int, float_n: number, data: number[][], corr: number[][], mean: number[], stddev: number[]): void {
  const eps = 0.1;

  for (let j = 0; j < m; j++) {
    mean[j] = 0.0;
    for (let i = 0; i < n; i++)
      mean[j] += data[i][j];
    mean[j] /= float_n;
  }

  for (let j = 0; j < m; j++) {
    stddev[j] = 0.0;
    for (let i = 0; i < n; i++)
      stddev[j] += (data[i][j] - mean[j]) * (data[i][j] - mean[j]);
    stddev[j] /= float_n;
    stddev[j] = Math.sqrt(stddev[j]);
    // The following in an inelegant but usual way to handle near-zero std. dev. values, which below would cause a
    // zero-divide.
    stddev[j] = stddev[j] <= eps ? 1.0 : stddev[j];
  }

  // Center and reduce the column vectors.
  for (let i = 0; i < n; i++)
    for (let j = 0; j < m; j++) {
      data[i][j] -= mean[j];
      data[i][j] /= Math.sqrt(float_n) * stddev[j];
    }

  // Calculate the m * m correlation matrix.
  for (let i = 0; i < m - 1; i++) {
    corr[i][i] = 1.0;
    for (let j = i + 1; j < m; j++) {
      corr[i][j] = 0.0;
      for (let k = 0; k < n; k++)
        corr[i][j] += (data[k][i] * data[k][j]);
      corr[j][i] = corr[i][j];
    }
  }
  corr[m - 1][m - 1] = 1.0;
}

function main(): void {
  const d = dataset();
  const n = N[d], m = M[d];

  const data: number[][] = Array.from({ length: n }, () => new Array<number>(m).fill(0));
  const corr: number[][] = Array.from({ length: m }, () => new Array<number>(m).fill(0));
  const mean: number[] = new Array<number>(m).fill(0);
  const stddev: number[] = new Array<number>(m).fill(0);

  const float_n = init_array(m, n, data);

  timerStart();
  kernel_correlation(m, n, float_n, data, corr, mean, stddev);
  timerStop();
  timerPrint();

  print_array(m, corr);
}

main();
