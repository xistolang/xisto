// countSubstrings' counters answer get from the packed keys and make the strings only when every value is needed:
// the same answers as counting with strings, before and after, for keys of another length or with other characters

import { Counter, countSubstrings } from "xisto:collections";

function text(n: int): string {
  const parts: string[] = [];
  let x = 17;
  for (let i = 0; i < n; i++) {
    x = (x * 1103 + 12345) % 2147483648;
    parts.push("acgtACGT".charAt(Math.trunc(x / 65536) % 8));
  }
  return parts.join("");
}

const seq = text(300000);
const [threes, fives, tens] = countSubstrings(seq, [3, 5, 10]);
console.log(threes.get("acg"), threes.get("ACG"), threes.get("ac"), threes.get("acgt"), threes.get("axg"), threes.get("é!x"));
console.log(fives.get("acgtA"), tens.get(seq.slice(100, 110)), tens.get(seq.slice(5000, 5010)));
console.log(threes.has("acg"), threes.has("zzz"), threes.size);
console.log(threes.get("acg"), threes.mostCommon().slice(0, 3).map(([k, c]) => `${k}=${c}`).join(" "));
fives.add("acgtA", 1000);
console.log(fives.get("acgtA"), fives.size);
const plain = new Counter<string>();
plain.deferTo((key: string) => (key === "x" ? 7 : 0), (into: Counter<string>) => into.add("x", 7));
console.log(plain.get("x"), plain.get("y"), plain.entries().map(([k, c]) => `${k}=${c}`).join(" "), plain.get("x"));
// a character the start of the text does not have, past the part looked at first: the whole scan, the same counts
const late = seq + "NNa" + seq.slice(0, 1000);
const [lone, lthrees] = countSubstrings(late, [1, 3]);
console.log(lone.get("N"), lone.get("a"), lthrees.get("NNa"), lthrees.get("aNN"), lone.size);
const wide = seq + "é" + "acg";
const [wone, wthrees] = countSubstrings(wide, [1, 3]);
console.log(wone.get("é"), wone.get("a"), wthrees.get("éac"), wthrees.get("acg"), wone.size);
