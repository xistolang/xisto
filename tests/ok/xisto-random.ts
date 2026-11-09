// xisto:random: the same values and characters as the plain loops, in Node (reference) and in xisto
import { LinearCongruential, WeightedCharacters } from "xisto:random";

const A = 3877, C = 29573, M = 139968;
const a = new LinearCongruential({ seed: 42, multiplier: A, increment: C, modulus: M });
console.log(a.next(), a.next(), a.next(), a.last);

// the plain loop of the fasta program: the first character whose running total is above value / modulus
function plain(seed: int, chars: string, probs: number[], count: int, width: int): string {
  let last = seed;
  const totals: number[] = [];
  let sum = 0;
  for (const p of probs) { sum += p; totals.push(sum); }
  let s = "", col = 0;
  for (let i = 0; i < count; i++) {
    last = (last * A + C) % M;
    const r = last / M;
    let k = 0;
    while (k < totals.length - 1 && !(r < totals[k])) k++;
    s += chars.slice(k, k + 1);
    if (width > 0 && ++col === width) { s += "\n"; col = 0; }
  }
  if (width > 0 && col > 0) s += "\n";
  return s;
}

const probs = [0.27, 0.12, 0.12, 0.27, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02];
const r = new LinearCongruential({ seed: 42, multiplier: A, increment: C, modulus: M });
const iub = new WeightedCharacters(r, "acgtBDHKMNRSVWY", probs);
let seed: int = 42, same = 0, total = 0;
const counts: int[] = [0, 1, 7, 60, 61, 130, 500, 1000, 300000, 139968];
const widths: int[] = [0, 0, 3, 60, 60, 60, 0, 7, 60, 0];
for (let t = 0; t < counts.length; t++) {
  const count = counts[t], width = widths[t];
  const got = iub.pick(count, { lineLength: width });
  if (got === plain(seed, "acgtBDHKMNRSVWY", probs, count, width)) same++;
  total++;
  // where the plain generator is now
  let last = seed;
  for (let i = 0; i < count; i++) last = (last * A + C) % M;
  seed = last;
  if (r.last !== seed) console.log("generator off after", count);
}
console.log("picks", total, "same", same);
console.log(JSON.stringify(iub.pick(25, { lineLength: 10 })));

// a generator whose multiplier shares a factor with the modulus: values that lead into the cycle and never return
const odd = new LinearCongruential({ seed: 5, multiplier: 6, increment: 1, modulus: 36 });
const oddText = new WeightedCharacters(odd, "xy", [0.5, 0.5]);
console.log(oddText.pick(40, { lineLength: 9 }).split("\n").join("|"));
// lines longer than the cycle, and a modulus above the size worked out in a table
const tiny = new WeightedCharacters(new LinearCongruential({ seed: 1, multiplier: 5, increment: 3, modulus: 16 }), "AB", [0.25, 0.75]);
console.log(tiny.pick(50, { lineLength: 20 }).split("\n").join("|"));
const big = new LinearCongruential({ seed: 7, multiplier: 48271, increment: 0, modulus: 2147483647 });
const coin = new WeightedCharacters(big, "HT", [0.5, 0.5]);
console.log(coin.pick(40, { lineLength: 16 }).split("\n").join("|"), big.last);

function tryIt(what: () => void): void {
  try { what(); console.log("no error"); } catch (e) { console.log((e as Error).message); }
}
tryIt(() => { new LinearCongruential({ seed: 10, multiplier: 3, increment: 1, modulus: 10 }); });
tryIt(() => { new LinearCongruential({ seed: 0, multiplier: 1 << 30, increment: 1, modulus: 1 << 30 }); });
tryIt(() => { new WeightedCharacters(a, "ab", [0.5]); });
tryIt(() => { new WeightedCharacters(a, "ab", [0.5, 0.6]); });
tryIt(() => { new WeightedCharacters(a, "é", [1]); });
tryIt(() => { iub.pick(-1); });
tryIt(() => { iub.pick(3, { lineLength: -2 }); });
