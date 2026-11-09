// XB-093: a pool callback that returns a number gets a region for the objects each call makes (a one-iteration loop
// around its body); objects of the same class made outside the callback stay on the heap (dual)
import { parallelFrom, parallelMap, parallelReduce } from "xisto:parallel";

class Cell {
  next: Cell | null;
  value: int;
  constructor(next: Cell | null, value: int) {
    this.next = next;
    this.value = value;
  }
}

function chain(n: int): Cell {
  let c = new Cell(null, 0);
  for (let i = 1; i < n; i++) c = new Cell(c, i);
  return c;
}

function total(first: Cell): int {
  let s: int = 0;
  let c: Cell | null = first;
  while (c !== null) {
    s += c.value;
    c = c.next;
  }
  return s;
}

const kept = chain(5); // on the heap, read after the pool calls
const sums = parallelFrom(40, (i) => total(chain(i + 1)));
const longest = parallelReduce(30, (i): number => Number(total(chain(i + 1))), (a, b) => Math.max(a, b), 0);
const sizes: int[] = [3, 1, 4, 1, 5];
const lens = parallelMap(sizes, (n): number => {
  const c = chain(n * 10);
  return total(c) / 2;
});
const odd = parallelFrom(9, (i) => total(chain(i + 1)) % 2 === 1);
console.log(sums.join(","));
console.log(longest, lens.join(","), odd.join(","), total(kept));
