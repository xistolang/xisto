// Generic functions over 3 types, 2M elements each: maxBy and mapAll (one instance per type in xisto).
interface Item {
  id: int;
  score: number;
}
function maxBy<T>(list: T[], key: (x: T) => number): T {
  let best = list[0];
  let bestKey = key(best);
  for (const x of list) {
    const k = key(x);
    if (k > bestKey) {
      best = x;
      bestKey = k;
    }
  }
  return best;
}
function mapAll<T, U>(list: T[], f: (x: T) => U): U[] {
  const out: U[] = [];
  for (const x of list) {
    out.push(f(x));
  }
  return out;
}
const nums: number[] = [];
const items: Item[] = [];
for (let i: int = 0; i < 2000000; i++) {
  nums.push(Number((i * 7919) % 10007));
  items.push({ id: i, score: Number((i * 31) % 997) });
}
const best = maxBy(items, (x: Item): number => x.score);
const m = maxBy(nums, (x: number): number => x);
const doubled = mapAll(nums, (x: number): number => x * 2);
const ids = mapAll(items, (x: Item): string => `#${x.id}`);
console.log(best.id, m, doubled.length, ids[1999999]);
