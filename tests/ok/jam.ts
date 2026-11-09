// parallelFill with a callback shaped like one row of a matrix-vector product: the compiler also makes a version
// that does eight rows at once (jam, XB-083); the sums must be the same doubles as one row at a time
import { parallelFill } from "xisto:parallel";

function A(i: int, j: int): number {
  return 1 / (Number((i + j) * (i + j + 1)) / 2 + Number(i) + 1);
}

function B(i: int, j: int): number {
  return Number((i - j) * (j - 2 * i)) / 7 - Number(j) * 0.5; // negative integers, subtraction
}

function show(name: string, v: number[]): void {
  let s = 0;
  for (let i = 0; i < v.length; i++) s += v[i] * (i + 1);
  console.log(name, v.length, v[0], v[v.length - 1], s);
}

const sizes: int[] = [1, 7, 8, 13, 100, 1003];
for (const n of sizes) {
  const u: number[] = [];
  for (let i = 0; i < n; i++) u.push(1 + (i % 5) / 3);
  const f = new Float64Array(n);
  for (let i = 0; i < n; i++) f[i] = u[i] * 0.25 - 0.1;
  const scale = 1.5;
  const k: int = 3;

  const a: number[] = new Array<number>(n).fill(0);
  parallelFill(a, (i) => {
    let t = 0;
    for (let j = 0; j < u.length; ++j) t += A(i, j) * u[j];
    return t;
  });
  show("A u", a);

  const b: number[] = new Array<number>(n).fill(0);
  parallelFill(b, (i) => {
    let t = 0.5;
    for (let j = 0; j < f.length; j++) t += B(j, i + k) * f[j] * scale;
    return t;
  });
  show("B f", b);

  const c: number[] = new Array<number>(n).fill(0);
  parallelFill(c, (i) => {
    let t = 0;
    for (let j = 0; j < u.length; j += 1) t += Number(-(i * j)) / (j + 1) + u[j];
    return t;
  });
  show("neg", c);

  const d: number[] = new Array<number>(n).fill(0);
  parallelFill(d, (i) => { // not the shape (two statements in the loop): the callback as written
    let t = 0;
    for (let j = 0; j < u.length; j++) {
      const x = A(j, i);
      t += x * u[j];
    }
    return t;
  });
  show("At u", d);
}
