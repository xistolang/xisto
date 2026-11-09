// countSubstrings around the packed limit (bits * length <= 46): 4 letters (2 bits) at lengths 22, 23 (packed)
// and 24, 25 (counted with strings), and 9 letters (4 bits) at 11 (packed) and 12 (strings); the counts and
// their order must be the same either way
import { countSubstrings } from "xisto:collections";

function sequence(letters: string, n: int, seed: int): string {
  let s = "";
  let x = seed;
  for (let i = 0; i < n; i++) {
    x = (x * 75 + 74) % 65537;
    const j = x % letters.length;
    s += letters.slice(j, j + 1);
  }
  return s;
}

for (const [letters, lengths] of [["ACGT", [1, 22, 23, 24, 25]], ["ABCDEFGHI", [2, 11, 12]]] as [string, int[]][]) {
  const text = sequence(letters, 3000, 7) + sequence(letters, 40, 7);
  const counters = countSubstrings(text, lengths);
  for (let i = 0; i < lengths.length; i++) {
    const top = counters[i].mostCommon().slice(0, 4);
    console.log(lengths[i], counters[i].size, top.map(([k, c]) => `${k}=${c}`).join(" "));
  }
}
