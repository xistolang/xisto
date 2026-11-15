// Iterator helpers (ES2025) on generators and on the iterators of arrays, Maps and Sets
function* naturals(): Generator<number> {
  let i = 0;
  while (true) {
    console.log("  yield", i);
    yield i++;
  }
}
// lazy: an infinite generator, only as many elements pulled as the chain needs
console.log(naturals().map((x) => x * 2).filter((x) => x % 3 === 0).take(3).toArray());
console.log(naturals().drop(2).take(2).toArray());
console.log(naturals().some((x) => x > 2), naturals().find((x) => x * x > 10));
console.log(naturals().every((x) => x < 2));
console.log(naturals().take(4).reduce((a, x) => a + x, 0), naturals().take(4).reduce((a, x) => a + x));
console.log(naturals().take(3).flatMap((x) => [x, -x]).toArray());
console.log(naturals().map((x, i) => x + i * 100).take(2).toArray());
naturals().take(2).forEach((x) => console.log("each", x));
naturals().filter((x, i) => i % 2 === 0).take(2).forEach((x, i) => console.log("even index", i, x));
// arrays
const xs = [3, 1, 4, 1, 5, 9, 2, 6];
console.log(xs.values().filter((x) => x > 2).map((x) => x * 10).toArray());
console.log(xs.values().drop(5).toArray(), xs.keys().take(3).toArray());
console.log(xs.values().reduce((a, x) => Math.max(a, x)));
// Maps and Sets
const m = new Map([["a", 1], ["b", 2], ["c", 3]]);
console.log(m.keys().take(2).toArray(), m.values().drop(1).toArray());
const s = new Set(["x", "y", "z"]);
console.log(s.values().drop(1).toArray());
try {
  naturals().drop(-1);
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
try {
  [1].values().take(1).reduce((a: number, x: number) => a + x);
  [0].values().drop(1).reduce((a: number, x: number) => a + x);
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
