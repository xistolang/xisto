/* The Computer Language Benchmarks Game
   https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

   Naive transliteration from Rex Kerr's Scala program
   contributed by Isaac Gouy
   (xisto: the same count with types, the permutations split in slices counted at the same time with
   xisto:parallel. Each slice starts from the permutation at its index, found from the factorial digits of the
   index (Oleg Mazurov's Java program does the same); the order is the original's, so the checksum is too.
   node/fannkuchredux-node-8.js is the original)
*/
import { parallelReduce } from "xisto:parallel";

/* what a slice of the permutations gives: the checksum and the most flips */
class Tally {
  checksum: int;
  maxFlips: int;
  constructor(checksum: int, maxFlips: int) {
    this.checksum = checksum;
    this.maxFlips = maxFlips;
  }
}

const n: int = process.argv.length > 2 ? Math.trunc(Number(process.argv[2])) : 7;

const factorial: int[] = [1];
for (let i = 1; i <= n; i++) factorial.push(factorial[i - 1] * i);

const sliceSize = Math.ceil(factorial[n] / 1000); // about a thousand slices: more than cores, so they even out
const slices = Math.ceil(factorial[n] / sliceSize);

/* the flips and checksum of the permutations [start, end) */
function countSlice(start: int, end: int): Tally {
  const perm1: int[] = new Array<int>(n).fill(0);
  const perm: int[] = new Array<int>(n).fill(0);
  const count: int[] = new Array<int>(n).fill(0);
  const before: int[] = new Array<int>(n).fill(0);
  for (let i = 0; i < n; i++) perm1[i] = i;

  // The permutation at index `start`: its factorial digits say how far to rotate each prefix
  let rest = start;
  for (let i = n - 1; i > 0; i--) {
    const d = Math.trunc(rest / factorial[i]);
    count[i] = d;
    rest = rest % factorial[i];
    for (let j = 0; j <= i; j++) before[j] = perm1[j];
    for (let j = 0; j <= i; j++) perm1[j] = j + d <= i ? before[j + d] : before[j + d - i - 1];
  }

  let checksum = 0, maxFlips = 0;
  for (let index = start; index < end; index++) {
    // Count flips and update max and checksum
    if (perm1[0] !== 0) {
      for (let i = 0; i < n; i++) perm[i] = perm1[i];
      let flips = 0;
      let k = perm[0];
      while (k !== 0) {
        for (let i = 0; 2 * i < k; i++) {
          const t = perm[i]; perm[i] = perm[k - i]; perm[k - i] = t;
        }
        k = perm[0];
        flips += 1;
      }
      if (flips > maxFlips) maxFlips = flips;
      if ((index & 0x1) === 0) checksum += flips; else checksum -= flips;
    }

    // The next permutation: rotate the shortest prefix whose counter has not run out
    let first = perm1[1];
    perm1[1] = perm1[0];
    perm1[0] = first;
    let i = 1;
    count[i] += 1;
    while (count[i] > i && i < n - 1) {
      count[i] = 0;
      i += 1;
      const next = perm1[1];
      perm1[0] = next;
      for (let j = 1; j < i; j++) perm1[j] = perm1[j + 1];
      perm1[i] = first;
      first = next;
      count[i] += 1;
    }
  }
  return new Tally(checksum, maxFlips);
}

const total = parallelReduce(
  slices,
  (s) => countSlice(s * sliceSize, Math.min((s + 1) * sliceSize, factorial[n])),
  (sum, slice) => new Tally(sum.checksum + slice.checksum, Math.max(sum.maxFlips, slice.maxFlips)),
  new Tally(0, 0),
);

console.log(total.checksum);
console.log(`Pfannkuchen(${n}) = ${total.maxFlips}`);
