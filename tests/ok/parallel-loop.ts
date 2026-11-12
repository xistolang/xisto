// xisto:parallel calls whose fn cannot be proved independent: each runs as the plain loop, with a warning
// warns: it assigns 'count', which is shared by every call
// warns: it calls console.log(), which acts outside the function
// warns: it uses 'box' (Box), shared by every call
// warns: it writes into 'cache', which is shared by every call
// warns: it calls a function value, whose body is not known here
// warns: fn reads 'v', the array being filled
// warns: the items are string[]
import { parallelMap, parallelFrom, parallelFill, parallelReduce } from "xisto:parallel";

class Box {
  value: number;
  constructor(value: number) {
    this.value = value;
  }
}

let count = 0;
const a = parallelFrom(5, (i): int => {
  count++;
  return i + count;
});
console.log(a.join(" "), count);

const b = parallelFrom(3, (i): int => {
  console.log("visiting", i);
  return i;
});
console.log(b.join(" "));

const box = new Box(2.5);
const c = parallelFrom(4, (i) => Number(i) * box.value);
console.log(c.join(" "));

const cache: number[] = [0, 0, 0, 0];
const d = parallelReduce(4, (i): number => {
  cache[i] = Number(i * i);
  return cache[i];
}, (t, x) => t + x, 0);
console.log(d, cache.join(","));

const twice: (x: int) => int = (x) => x * 2; // (a const of a function type stays a function value)
console.log(parallelFrom(4, (i) => twice(i)).join(" "));

const v: number[] = [1, 1, 1, 1, 1];
parallelFill(v, (i) => (i > 0 ? v[i - 1] * 2 : 1));
console.log(v.join(" "));

console.log(parallelMap(["a", "bb", "ccc"], (s) => s.length).join(","));
