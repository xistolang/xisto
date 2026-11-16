// Object.groupBy / Map.groupBy (ES2024) and the Set methods of ES2025
interface Item { name: string; kind: string; qty: number }
const items: Item[] = [
  { name: "apple", kind: "fruit", qty: 3 },
  { name: "leek", kind: "vegetable", qty: 1 },
  { name: "pear", kind: "fruit", qty: 0 },
  { name: "kale", kind: "vegetable", qty: 7 },
];
const byKind = Object.groupBy(items, (it) => it.kind);
console.log(byKind);
console.log(Object.keys(byKind), byKind["fruit"]?.length);
const parity = Object.groupBy([1, 2, 3, 4, 5], (x) => (x % 2 === 1 ? "odd" : "even"));
console.log(parity);
console.log(Object.groupBy([] as number[], (x) => String(x)));
console.log(Object.groupBy(new Set(["a", "bb", "cc"]), (s, i) => s.length + ":" + (i % 2)));
const stock = Map.groupBy(items, (it) => (it.qty > 0 ? "in" : "out"));
console.log(stock.get("in")?.map((it) => it.name), stock.get("out")?.map((it) => it.name));
console.log(Map.groupBy([1, 2, 3, 4, 5, 6], (x) => x % 3));
// Set methods: the result's order follows the specification's steps
const a = new Set([1, 2, 3, 4]);
const b = new Set([6, 4, 2]);
const big = new Set([9, 8, 7, 4, 3, 2, 1]);
console.log(a.union(b), b.union(a));
console.log(a.intersection(b), a.intersection(big), big.intersection(a));
console.log(a.difference(b), a.difference(big), big.difference(a));
console.log(a.symmetricDifference(b), b.symmetricDifference(a));
console.log(a.isSubsetOf(big), big.isSubsetOf(a), a.isSubsetOf(a), new Set<number>().isSubsetOf(a));
console.log(big.isSupersetOf(a), a.isSupersetOf(b), a.isDisjointFrom(b), a.isDisjointFrom(new Set([10, 11])));
const s1 = new Set(["x", "y"]);
const s2 = new Set(["y", "z"]);
console.log(s1.union(s2), s1.intersection(s2), s1.difference(s2), s1.symmetricDifference(s2), s1);
