// PolyBench/C 4.2.1, medley/deriche (deriche.c, deriche.h), with types: the same loops in the same order, float as
// in C (DATA_TYPE_IS_FLOAT): the arrays are Float32Array[] (one per row, as number[][] elsewhere), and the float
// arithmetic of the C is Math.fround on each operation (a store in a Float32Array already rounds).
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpArrays, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, fmt2, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (deriche.h); LARGE unless the command line names another
const W: int[] = [64, 192, 720, 4096, 7680];
const H: int[] = [64, 128, 480, 2160, 4320];

function init_array(w: int, h: int, imgIn: Float32Array[], imgOut: Float32Array[]): void {
  // input should be between 0 and 1 (grayscale image pixel)
  for (let i = 0; i < w; i++)
    for (let j = 0; j < h; j++)
      imgIn[i][j] = ((313 * i + 991 * j) % 65536) / 65535.0;
}

function print_array(w: int, h: int, imgOut: Float32Array[]): void {
  dumpStart();
  dumpBegin("imgOut");
  for (let i = 0; i < w; i++)
    for (let j = 0; j < h; j++) {
      if ((i * h + j) % 20 === 0) dumpWrite("\n");
      dumpWrite(fmt2(imgOut[i][j]) + " ");
    }
  dumpEnd("imgOut");
  dumpFinish();
}

// Original code provided by Gael Deest
function kernel_deriche(w: int, h: int, alpha: number, imgIn: Float32Array[], imgOut: Float32Array[], y1: Float32Array[], y2: Float32Array[]): void {
  // Each float operation of the C rounds to float, so each one goes through Math.fround: rounded only where a value is
  // stored, k comes out 1 to 2 ulp off (the denominator cancels), a2 and a3 1 ulp off, and most of imgOut differs
  // from the C in the last bits (one "%0.2f" of LARGE changes). With one Math.fround per operation, imgOut is the C's
  // bit for bit (checked with "%a" in MINI and MEDIUM).
  const k = Math.fround(Math.fround(Math.fround(1.0 - Math.fround(Math.exp(-alpha))) * Math.fround(1.0 - Math.fround(Math.exp(-alpha)))) /
    Math.fround(Math.fround(1.0 + Math.fround(Math.fround(2.0 * alpha) * Math.fround(Math.exp(-alpha)))) - Math.fround(Math.exp(2.0 * alpha))));
  const a1 = k, a5 = k;
  const a2 = Math.fround(Math.fround(k * Math.fround(Math.exp(-alpha))) * (alpha - 1.0)), a6 = a2;
  const a3 = Math.fround(Math.fround(k * Math.fround(Math.exp(-alpha))) * (alpha + 1.0)), a7 = a3;
  const a4 = Math.fround(-k * Math.fround(Math.exp(-2.0 * alpha))), a8 = a4;
  const b1 = Math.fround(Math.pow(2.0, -alpha));
  const b2 = Math.fround(-Math.exp(-2.0 * alpha));
  const c1 = 1, c2 = 1;

  for (let i = 0; i < w; i++) {
    let ym1 = 0.0;
    let ym2 = 0.0;
    let xm1 = 0.0;
    for (let j = 0; j < h; j++) {
      y1[i][j] = Math.fround(Math.fround(Math.fround(a1 * imgIn[i][j]) + Math.fround(a2 * xm1)) + Math.fround(b1 * ym1)) + Math.fround(b2 * ym2);
      xm1 = imgIn[i][j];
      ym2 = ym1;
      ym1 = y1[i][j];
    }
  }
  for (let i = 0; i < w; i++) {
    let yp1 = 0.0;
    let yp2 = 0.0;
    let xp1 = 0.0;
    let xp2 = 0.0;
    for (let j = h - 1; j >= 0; j--) {
      y2[i][j] = Math.fround(Math.fround(Math.fround(a3 * xp1) + Math.fround(a4 * xp2)) + Math.fround(b1 * yp1)) + Math.fround(b2 * yp2);
      xp2 = xp1;
      xp1 = imgIn[i][j];
      yp2 = yp1;
      yp1 = y2[i][j];
    }
  }
  for (let i = 0; i < w; i++)
    for (let j = 0; j < h; j++) {
      imgOut[i][j] = c1 * (y1[i][j] + y2[i][j]);
    }
  for (let j = 0; j < h; j++) {
    let tm1 = 0.0;
    let ym1 = 0.0;
    let ym2 = 0.0;
    for (let i = 0; i < w; i++) {
      y1[i][j] = Math.fround(Math.fround(Math.fround(a5 * imgOut[i][j]) + Math.fround(a6 * tm1)) + Math.fround(b1 * ym1)) + Math.fround(b2 * ym2);
      tm1 = imgOut[i][j];
      ym2 = ym1;
      ym1 = y1[i][j];
    }
  }
  for (let j = 0; j < h; j++) {
    let tp1 = 0.0;
    let tp2 = 0.0;
    let yp1 = 0.0;
    let yp2 = 0.0;
    for (let i = w - 1; i >= 0; i--) {
      y2[i][j] = Math.fround(Math.fround(Math.fround(a7 * tp1) + Math.fround(a8 * tp2)) + Math.fround(b1 * yp1)) + Math.fround(b2 * yp2);
      tp2 = tp1;
      tp1 = imgOut[i][j];
      yp2 = yp1;
      yp1 = y2[i][j];
    }
  }
  for (let i = 0; i < w; i++)
    for (let j = 0; j < h; j++)
      imgOut[i][j] = c2 * (y1[i][j] + y2[i][j]);
}

function main(): void {
  const d = dataset();
  const w = W[d], h = H[d];

  const alpha = 0.25; // parameter of the filter
  const imgIn: Float32Array[] = Array.from({ length: w }, () => new Float32Array(h));
  const imgOut: Float32Array[] = Array.from({ length: w }, () => new Float32Array(h));
  const y1: Float32Array[] = Array.from({ length: w }, () => new Float32Array(h));
  const y2: Float32Array[] = Array.from({ length: w }, () => new Float32Array(h));

  init_array(w, h, imgIn, imgOut);

  timerStart();
  kernel_deriche(w, h, alpha, imgIn, imgOut, y1, y2);
  timerStop();
  timerPrint();

  if (dumpArrays()) print_array(w, h, imgOut);
}

main();
