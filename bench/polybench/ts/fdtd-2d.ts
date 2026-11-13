// PolyBench/C 4.2.1, stencils/fdtd-2d (fdtd-2d.c, fdtd-2d.h), with types: the same loops in the same order.
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpBegin, dumpEnd, dumpFinish, dumpFlush, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (fdtd-2d.h); LARGE unless the command line names another
const TMAX: int[] = [20, 40, 100, 500, 1000];
const NX: int[] = [20, 60, 200, 1000, 2000];
const NY: int[] = [30, 80, 240, 1200, 2600];

function init_array(tmax: int, nx: int, ny: int, ex: number[][], ey: number[][], hz: number[][], _fict_: number[]): void {
  for (let i = 0; i < tmax; i++)
    _fict_[i] = i;
  for (let i = 0; i < nx; i++)
    for (let j = 0; j < ny; j++) {
      ex[i][j] = (i * (j + 1)) / nx;
      ey[i][j] = (i * (j + 2)) / ny;
      hz[i][j] = (i * (j + 3)) / nx;
    }
}

function print_array(nx: int, ny: int, ex: number[][], ey: number[][], hz: number[][]): void {
  dumpStart();
  dumpBegin("ex");
  for (let i = 0; i < nx; i++)
    for (let j = 0; j < ny; j++) {
      if ((i * nx + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(ex[i][j]) + " ");
    }
  dumpEnd("ex");
  dumpFinish();
  // as in the C: ey and hz come after POLYBENCH_DUMP_FINISH
  dumpBegin("ey");
  for (let i = 0; i < nx; i++)
    for (let j = 0; j < ny; j++) {
      if ((i * nx + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(ey[i][j]) + " ");
    }
  dumpEnd("ey");
  dumpBegin("hz");
  for (let i = 0; i < nx; i++)
    for (let j = 0; j < ny; j++) {
      if ((i * nx + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(hz[i][j]) + " ");
    }
  dumpEnd("hz");
  dumpFlush();
}

function kernel_fdtd_2d(tmax: int, nx: int, ny: int, ex: number[][], ey: number[][], hz: number[][], _fict_: number[]): void {
  for (let t = 0; t < tmax; t++) {
    for (let j = 0; j < ny; j++)
      ey[0][j] = _fict_[t];
    for (let i = 1; i < nx; i++)
      for (let j = 0; j < ny; j++)
        ey[i][j] = ey[i][j] - 0.5 * (hz[i][j] - hz[i - 1][j]);
    for (let i = 0; i < nx; i++)
      for (let j = 1; j < ny; j++)
        ex[i][j] = ex[i][j] - 0.5 * (hz[i][j] - hz[i][j - 1]);
    for (let i = 0; i < nx - 1; i++)
      for (let j = 0; j < ny - 1; j++)
        hz[i][j] = hz[i][j] - 0.7 * (ex[i][j + 1] - ex[i][j] + ey[i + 1][j] - ey[i][j]);
  }
}

function main(): void {
  const d = dataset();
  const tmax = TMAX[d], nx = NX[d], ny = NY[d];

  const ex: number[][] = Array.from({ length: nx }, () => new Array<number>(ny).fill(0));
  const ey: number[][] = Array.from({ length: nx }, () => new Array<number>(ny).fill(0));
  const hz: number[][] = Array.from({ length: nx }, () => new Array<number>(ny).fill(0));
  const _fict_: number[] = new Array<number>(tmax).fill(0);

  init_array(tmax, nx, ny, ex, ey, hz, _fict_);

  timerStart();
  kernel_fdtd_2d(tmax, nx, ny, ex, ey, hz, _fict_);
  timerStop();
  timerPrint();

  print_array(nx, ny, ex, ey, hz);
}

main();
