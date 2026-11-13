// PolyBench/C 4.2.1, medley/nussinov (nussinov.c, nussinov.h), with types: the same loops in the same order, int as in C (DATA_TYPE_IS_INT: each row of table an Int32Array, 32-bit ints as C's; base, a char in C, is an int).
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
// Derived from PolyBench/C under the Ohio State University Software Distribution License: the conditions and
// the disclaimer are in bench/polybench/LICENSE-PolyBench.txt.
import { dataset, dumpArrays, dumpBegin, dumpEnd, dumpFinish, dumpStart, dumpWrite, timerPrint, timerStart, timerStop } from "./lib/polybench.ts";

// MINI, SMALL, MEDIUM, LARGE, EXTRALARGE (nussinov.h); LARGE unless the command line names another
const N: int[] = [60, 180, 500, 2500, 5500];

// the C macros match and max_score
function match(b1: int, b2: int): int {
  return b1 + b2 === 3 ? 1 : 0;
}

function max_score(s1: int, s2: int): int {
  return s1 >= s2 ? s1 : s2;
}

function init_array(n: int, seq: int[], table: Int32Array[]): void {
  // base is AGCT/0..3
  for (let i = 0; i < n; i++) {
    seq[i] = (i + 1) % 4;
  }
  for (let i = 0; i < n; i++)
    for (let j = 0; j < n; j++)
      table[i][j] = 0;
}

function print_array(n: int, table: Int32Array[]): void {
  let t = 0;
  dumpStart();
  dumpBegin("table");
  for (let i = 0; i < n; i++) {
    for (let j = i; j < n; j++) {
      if (t % 20 === 0) dumpWrite("\n");
      dumpWrite(`${table[i][j]} `);
      t++;
    }
  }
  dumpEnd("table");
  dumpFinish();
}

// Original version by Dave Wonnacott at Haverford College <davew@cs.haverford.edu>, with help from Allison Lake,
// Ting Zhou, and Tian Jin, based on algorithm by Nussinov, described in Allison Lake's senior thesis.
function kernel_nussinov(n: int, seq: int[], table: Int32Array[]): void {
  for (let i = n - 1; i >= 0; i--) {
    for (let j = i + 1; j < n; j++) {
      if (j - 1 >= 0)
        table[i][j] = max_score(table[i][j], table[i][j - 1]);
      if (i + 1 < n)
        table[i][j] = max_score(table[i][j], table[i + 1][j]);
      if (j - 1 >= 0 && i + 1 < n) {
        // don't allow adjacent elements to bond
        if (i < j - 1)
          table[i][j] = max_score(table[i][j], table[i + 1][j - 1] + match(seq[i], seq[j]));
        else
          table[i][j] = max_score(table[i][j], table[i + 1][j - 1]);
      }
      for (let k = i + 1; k < j; k++) {
        table[i][j] = max_score(table[i][j], table[i][k] + table[k + 1][j]);
      }
    }
  }
}

function main(): void {
  const n = N[dataset()];

  const seq: int[] = new Array<int>(n).fill(0);
  const table: Int32Array[] = Array.from({ length: n }, () => new Int32Array(n));

  init_array(n, seq, table);

  timerStart();
  kernel_nussinov(n, seq, table);
  timerStop();
  timerPrint();

  if (dumpArrays()) print_array(n, table);
}

main();
