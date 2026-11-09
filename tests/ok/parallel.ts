// xisto:parallel on the pool: results in index order, combine in order, errors as in the loop
// warns: none
import { parallelMap, parallelFrom, parallelFill, parallelReduce } from "xisto:parallel";

class Point {
  x: number;
  y: number;
  constructor(x: number, y: number) {
    this.x = x;
    this.y = y;
  }
  norm(): number {
    return Math.sqrt(this.x * this.x + this.y * this.y);
  }
}

const n = 10000;
const weights: number[] = [];
for (let i = 0; i < n; i++) weights.push((i % 7) + 0.5);

// parallelFrom: numbers, strings, objects, typed arrays
const squares = parallelFrom(n, (i) => Number(i * i));
let sum = 0;
for (const s of squares) sum += s;
console.log(squares.length, squares[0], squares[n - 1], sum);
const labels = parallelFrom(5, (i) => `item ${i}`);
console.log(labels.join(", "));
const points = parallelFrom(1000, (i) => new Point(Number(i), Number(i * 2)));
console.log(points[999].x, points[999].y, points[3].norm().toFixed(6));
const rows = parallelFrom(4, (y): Uint8Array => {
  const row = new Uint8Array(3);
  for (let x = 0; x < 3; x++) row[x] = y * 10 + x;
  return row;
});
console.log(rows.map((r) => r.join("-")).join(" "));
console.log(parallelFrom(0, (i) => i).length);

// parallelMap over numbers, with and without the index; a shared array read by index
const scaled = parallelMap(weights, (wt, i) => wt * Number(i));
console.log(scaled[1], scaled[8], scaled[n - 1]);
const halves = parallelMap([2, 4, 6], (v) => v / 2);
console.log(halves.join(" "));
const picks: int[] = [0, 3, 6];
const lookups = parallelMap(picks, (k) => weights[k] + Number(weights.length));
console.log(lookups.join(" "));

// parallelFill: every slot, in place; a helper function reading a shared array
function dot(row: int, v: number[]): number {
  let t = 0;
  for (let j = 0; j < v.length; j++) t += Number((row + j) % 3) * v[j];
  return t;
}
const out: number[] = new Array<number>(8).fill(0);
parallelFill(out, (i) => dot(i, weights));
console.log(out.join(" "));

// parallelReduce: combine runs in index order (not commutative here), on the calling thread
const text = parallelReduce(12, (i) => String.fromCharCode(97 + i), (acc, c) => acc + c, ">");
console.log(text);
const total = parallelReduce(n, (i) => weights[i] * 2, (a, b) => a + b, 0);
console.log(total);
const order: number[] = [];
const last = parallelReduce(6, (i) => Number(i * 3), (a, b): number => { order.push(b); return b; }, -1);
console.log(last, order.join(","));

// nested: the inner call runs as a loop inside each outer call
const grid = parallelFrom(4, (y) => parallelFrom(3, (x) => y * 3 + x).join(""));
console.log(grid.join("|"));

// errors: the lowest failing index wins, as in the loop
try {
  parallelFrom(n, (i): int => {
    if (i % 1000 === 777) throw new Error(`bad index ${i}`);
    return i;
  });
} catch (e) {
  console.log("caught:", (e as Error).message);
}
const partial: number[] = new Array<number>(n).fill(-1);
try {
  parallelFill(partial, (i): number => {
    if (i >= 5000 && i % 2 === 0) throw new Error(`fill failed at ${i}`);
    return Number(i);
  });
} catch (e) {
  console.log("caught:", (e as Error).message, partial[4999], partial[5000], partial[5001]);
}
const seen: number[] = [];
try {
  parallelReduce(100, (i): number => {
    if (i === 40 || i === 90) throw new Error(`reduce failed at ${i}`);
    return Number(i);
  }, (a, b): number => { seen.push(b); return a + b; }, 0);
} catch (e) {
  console.log("caught:", (e as Error).message, seen.length, seen[seen.length - 1]);
}
try {
  parallelFrom(-1, (i) => i);
} catch (e) {
  console.log("caught:", (e as Error).message);
}
