// Top-level const arrays the program only indexes: their length is known, and a table of int literals that is
// never written gives its entries a range, so x[T[p]] needs no check when every entry is below x's length.
// The answers are the same either way; what changes is the generated code (bce_safe), so the cases below also
// cover what must stay checked: an entry out of range, a table that is written, an array that is passed on.
const N: int = 5;
const PAIRS: int = 10;
const PI_: int[] = [0, 0, 0, 0, 1, 1, 1, 2, 2, 3];
const PJ_: int[] = [1, 2, 3, 4, 2, 3, 4, 3, 4, 4];
const BIG: int[] = [0, 7, 2]; // 7 is not below 5: x[BIG[k]] stays checked
const NEG: int[] = [-1, 0, 1]; // a negative entry: checked

const x: number[] = [1.5, 2.5, 3.5, 4.5, 5.5];
const acc: number[] = new Array<number>(PAIRS).fill(0);
const hits: int[] = new Array<int>(N).fill(0);
let written: int[] = [1, 2, 3]; // let: not a const table
const grows: int[] = [0, 1]; // passed to a function: it may grow there
const shared: number[] = [1, 2, 3, 4, 5];

function push3(a: int[]): void {
  a.push(3);
}
function sum(a: number[]): number {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += a[i];
  return s;
}

function pairs(): number {
  let t = 0;
  for (let p = 0; p < PAIRS; p++) {
    const i = PI_[p], j = PJ_[p];
    acc[p] = x[i] * x[j];
    hits[i]++;
    hits[j] += 1;
    t += acc[p];
  }
  return t;
}

function safeBig(k: int): number {
  if (k < 0 || k >= BIG.length) return -1;
  const i = BIG[k];
  return i < x.length ? x[i] : -2;
}

function safeNeg(k: int): number {
  const i = NEG[k];
  return i >= 0 ? x[i] : -3;
}

function energy(): number {
  let e = 0;
  for (let i = 0; i < N; i++) {
    e += 0.5 * x[i];
    for (let j = i + 1; j < N; j++) e -= x[i] * x[j];
  }
  return e;
}

written[1] = 9;
push3(grows);
console.log(pairs(), hits.join(","));
console.log(safeBig(0), safeBig(1), safeBig(2), safeBig(3));
console.log(safeNeg(0), safeNeg(1), safeNeg(2));
console.log(energy(), written[1], grows.length, sum(shared));
console.log(acc.length, acc[9], x[PI_[PAIRS - 1]] + x[PJ_[0]]);
