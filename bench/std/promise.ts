// Promise.all under load: 10k tasks waiting on timers at once, then 200 rounds of 50 small concurrent
// computations (a task's start and finish are what costs).
import { setTimeout as sleep } from "node:timers/promises";

async function waitAndSquare(i: int): Promise<int> {
  await sleep(5 + (i % 10));
  return i * i;
}
async function square(i: int): Promise<int> {
  return i * i;
}
async function main(): Promise<void> {
  const ids: int[] = [];
  for (let i: int = 0; i < 10000; i++) {
    ids.push(i);
  }
  const waited = await Promise.all(ids.map((i: int) => waitAndSquare(i)));
  let total: int = 0;
  for (const v of waited) {
    total += v % 1000;
  }
  const small: int[] = [];
  for (let i: int = 0; i < 50; i++) {
    small.push(i);
  }
  for (let round: int = 0; round < 200; round++) {
    const r = await Promise.all(small.map((i: int) => square(i + round)));
    total += r[49] % 7;
  }
  console.log("total", total);
}
main();
