// xisto:collections: Counter and countSubstrings, the same counts and order in Node and in xisto
// warns: none
import { Counter, countSubstrings } from "xisto:collections";

const words = new Counter<string>();
for (const w of "the cat and the hat and the bat".split(" ")) words.add(w);
words.add("dog", 0);
words.add("cat", -1);
console.log(words.get("the"), words.get("and"), words.get("cat"), words.get("dog"), words.get("cow"));
console.log(words.has("dog"), words.has("cow"), words.size);
console.log(words.entries());
console.log(words.mostCommon());

const nums = new Counter<int>();
const values: int[] = [3, 1, 3, 2, 1, 3];
for (const n of values) nums.add(n);
nums.add(7, 5);
console.log(nums.mostCommon(), nums.get(4));

// small texts: every length, ties in the order of first occurrence
for (const text of ["", "a", "aaa", "GATTACA", "banana", "abcabcabc", "the quick brown fox"]) {
  const counts = countSubstrings(text, [0, 1, 2, 3, 8, -1]);
  console.log(JSON.stringify(text), counts.map((c) => c.size).join(","), JSON.stringify(counts[1].mostCommon()), JSON.stringify(counts[3].entries()));
}

// not ASCII: counted with strings
const uni = countSubstrings("ação, ação e coração", [1, 3]);
console.log(uni[0].mostCommon().slice(0, 5), uni[1].get("ção"), uni[1].size);

// too long to pack (2 bits x 30 = 60 > 52): counted with strings
const dna = "ACGTTGCAACGTAGCTAGCTTTACGGACT".repeat(4);
const long = countSubstrings(dna, [30, 2]);
console.log(long[0].size, long[0].get(dna.slice(0, 30)), long[1].mostCommon().slice(0, 4));

// long enough to be counted in pieces on several threads: first occurrences across pieces stay in order
let seed = 42;
let big = "";
const letters = "ACGT";
const parts: string[] = [];
for (let i = 0; i < 400000; i++) {
  seed = (seed * 3877 + 29573) % 139968;
  const r = Math.trunc(seed * 4 / 139968);
  parts.push(letters.slice(r, r + 1));
}
big = parts.join("");
const lengths: int[] = [1, 2, 3, 4, 6, 8, 9, 12, 18];
const all = countSubstrings(big, lengths);
for (let i = 0; i < lengths.length; i++) {
  const c = all[i];
  const top = c.mostCommon();
  let sum: int = 0;
  for (const [, n] of c.entries()) sum += n;
  console.log(lengths[i], c.size, sum, JSON.stringify(top.slice(0, 3)), JSON.stringify(c.entries().slice(0, 3)), c.get(big.slice(1000, 1000 + lengths[i])));
}

// a text of more letters (5: 3 bits a character), in pieces
const five = big.replaceAll("T", "TN").slice(0, 300000);
const f = countSubstrings(five, [1, 5, 10, 17]);
console.log(f.map((c) => c.size).join(","), JSON.stringify(f[0].mostCommon()), f[2].get(five.slice(500, 510)), f[3].get(five.slice(7, 24)));
