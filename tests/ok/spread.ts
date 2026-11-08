// spread in array literals: arrays, empty arrays, Uint8Array, Float64Array, strings (code points), Set, Map
// (entries and keys), mixed with plain elements; the result is a new array (the sources do not change)
const a = [1, 2];
const empty: number[] = [];
console.log([...a, ...empty, 3].join(","), [...empty].length, [...a].length);
const u = new Uint8Array([7, 8]);
const f = new Float64Array([1.5, -0.25]);
console.log([...u, 9].join(","), [...f, 2].join(","));
const m = new Map<string, number>([["x", 1], ["y", 2]]);
const st = new Set<string>(["p", "q"]);
console.log([...m.keys()].join(","), [...st].join(","), [..."aé😀b"].join("|"), [..."aé😀b"].length);
console.log([...m].map((kv) => kv[0] + String(kv[1])).join(";"));
const words = ["a", "b"];
const w2 = [...words, "c", ...words];
w2.push("d");
console.log(words.join(","), w2.join(","));
const nested = [[1], [2, 3]];
const flat = [...nested[0], ...nested[1]];
console.log(flat.join(","));
interface Cell {
  n: number;
}
const objs: Cell[] = [{ n: 1 }, { n: 2 }];
const more: Cell[] = [...objs, { n: 3 }];
more[0].n = 10; // the same objects: a shallow copy, as in JavaScript
console.log(objs[0].n, more.length);
