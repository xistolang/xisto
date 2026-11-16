// ES2022-ES2023 array methods: at, findLast, findLastIndex, toSorted, toReversed, with, toSpliced, copyWithin,
// values, keys, Array.of, and String.prototype.at
const xs = [5, 1, 4, 2, 3];
console.log(xs.at(0), xs.at(-1), xs.at(-5), xs.at(5) ?? "none", xs.at(-6) ?? "none");
const words = ["pear", "fig", "apple", "kiwi"];
console.log(words.at(-2), words.at(10) === undefined);
console.log(xs.findLast((x) => x > 3), xs.findLastIndex((x) => x > 3), xs.findLastIndex((x) => x > 9));
console.log(words.findLast((w) => w.length === 4), words.findLast((w) => w.length > 9) ?? "none");
console.log(words.findLastIndex((w, i) => i < 2 && w.length > 2));
// copies: the array itself is not changed
const sorted = xs.toSorted((a, b) => a - b);
const rev = xs.toReversed();
console.log(sorted, rev, xs);
console.log(words.toSorted(), words);
const w2 = xs.with(1, 10);
const w3 = xs.with(-1, 30);
console.log(w2, w3, xs);
try {
  xs.with(5, 0);
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
try {
  xs.with(-6, 0);
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
console.log(xs.toSpliced(1, 2), xs.toSpliced(1, 0, 7, 8), xs.toSpliced(-2, 1, 9), xs.toSpliced(2), xs.toSpliced(), xs);
console.log(xs.toSpliced(10, 1, 6), xs.toSpliced(0, 99));
const cw = [1, 2, 3, 4, 5];
console.log(cw.copyWithin(0, 3), [1, 2, 3, 4, 5].copyWithin(1, 0, 3), [1, 2, 3, 4, 5].copyWithin(-2, 0), cw);
// iterators over an array
for (const v of words.values()) console.log("value", v);
for (const k of words.keys()) console.log("key", k);
const it = xs.values();
let total = 0;
for (const v of it) total += v;
console.log(total);
console.log(Array.of(1, 2, 3), Array.of("a"));
const s = new Set([3, 1, 2]);
console.log(Array.from(s, (x) => x * 10), Array.from(new Map([["a", 1]]).keys()));
// strings
const str = "hello";
console.log(str.at(0), str.at(-1), str.at(-5), str.at(5) ?? "none", "".at(0) ?? "none");
console.log("ab".isWellFormed(), "ab".toWellFormed());
