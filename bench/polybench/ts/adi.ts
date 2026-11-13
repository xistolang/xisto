// PolyBench/C 4.2.1, stencils/adi (adi.c, adi.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (adi.h); LARGE unless the command line names another
const TSTEPS: int[] = [20, 40, 100, 500, 1000];
const N: int[] = [20, 60, 200, 1000, 2000];

function init_array(n: int, u: number[][]): void {
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      u[i][j] = (i + n - j) / n;
    }
}

function print_array(n: int, u: number[][]): void {
  dumpStart();
  dumpBegin("u");
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++) {
      if ((i * n + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(u[i][j]) + " ");
    }
  dumpEnd("u");
  dumpFinish();
}

// Based on a Fortran code fragment from Figure 5 of "Automatic Data and Computation Decomposition on Distributed
// Memory Parallel Computers" by Peizong Lee and Zvi Meir Kedem, TOPLAS, 2002
function kernel_adi(tsteps: int, n: int, u: number[][], v: number[][], p: number[][], q: number[][]): void {
  const DX = 1.0 / n;
  const DY = 1.0 / n;
  const DT = 1.0 / tsteps;
  const B1 = 2.0;
  const B2 = 1.0;
  const mul1 = B1 * DT / (DX * DX);
  const mul2 = B2 * DT / (DY * DY);
  const a = -mul1 / 2.0;
  const b = 1.0 + mul1;
  const c = a;
  const d = -mul2 / 2.0;
  const e = 1.0 + mul2;
  const f = d;

  for (let t = 1; t <= tsteps; t++) {
    // Column Sweep
    for (let i = 1; i < n - 1; i++) {
      v[0][i] = 1.0;
      p[i][0] = 0.0;
      q[i][0] = v[0][i];
      for (let j = 1; j < n - 1; j++) {
        p[i][j] = -c / (a * p[i][j - 1] + b);
        q[i][j] = (-d * u[j][i - 1] + (1.0 + 2.0 * d) * u[j][i] - f * u[j][i + 1] - a * q[i][j - 1]) / (a * p[i][j - 1] + b);
      }
      v[n - 1][i] = 1.0;
      for (let j = n - 2; j >= 1; j--) {
        v[j][i] = p[i][j] * v[j + 1][i] + q[i][j];
      }
    }
    // Row Sweep
    for (let i = 1; i < n - 1; i++) {
      u[i][0] = 1.0;
      p[i][0] = 0.0;
      q[i][0] = u[i][0];
      for (let j = 1; j < n - 1; j++) {
        p[i][j] = -f / (d * p[i][j - 1] + e);
        q[i][j] = (-a * v[i - 1][j] + (1.0 + 2.0 * a) * v[i][j] - c * v[i + 1][j] - d * q[i][j - 1]) / (d * p[i][j - 1] + e);
      }
      u[i][n - 1] = 1.0;
      for (let j = n - 2; j >= 1; j--) {
        u[i][j] = p[i][j] * u[i][j + 1] + q[i][j];
      }
    }
  }
}

function main(): void {
  const d = dataset();
  const n = N[d];
  const tsteps = TSTEPS[d];

  const u: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const v: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const p: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));
  const q: number[][] = Array.from({ length: n }, () => new Array<number>(n).fill(0));

  init_array(n, u);

  timerStart();
  kernel_adi(tsteps, n, u, v, p, q);
  timerStop();
  timerPrint();

  print_array(n, u);
}

main();
