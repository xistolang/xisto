// Closures that share mutable state: counters and accumulators, 10M updates through captured variables.
function makeCounter(): () => int {
  let n: int = 0;
  return (): int => {
    n++;
    return n;
  };
}
let total: int = 0;
const xs: int[] = [];
for (let i: int = 0; i < 1000; i++) {
  xs.push(i);
}
for (let round: int = 0; round < 10000; round++) {
  xs.forEach((x: int): void => {
    total += x;
  });
}
const c = makeCounter();
let last: int = 0;
for (let i: int = 0; i < 1000000; i++) {
  last = c();
}
console.log(total, last);
