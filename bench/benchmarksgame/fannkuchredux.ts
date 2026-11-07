/* The Computer Language Benchmarks Game
   https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

   Naive transliteration from Rex Kerr's Scala program
   contributed by Isaac Gouy
   (xisto: the same program with types; node/fannkuchredux-node-8.js is the original. There `[n]` makes an
   array that grows as it is written; here the arrays have their n elements from the start)
*/

function fannkuch(n: int): int {
  const perm1: int[] = new Array<int>(n).fill(0);
  for (let i = 0; i < n; i++) perm1[i] = i;
  const perm: int[] = new Array<int>(n).fill(0);
  const count: int[] = new Array<int>(n).fill(0);
  let f = 0, flips = 0, nperm = 0, checksum = 0;
  let i = 0, k = 0, r = 0;

  r = n;
  while (r > 0) {
    i = 0;
    while (r !== 1) { count[r - 1] = r; r -= 1; }
    while (i < n) { perm[i] = perm1[i]; i += 1; }

    // Count flips and update max and checksum
    f = 0;
    k = perm[0];
    while (k !== 0) {
      i = 0;
      while (2 * i < k) {
        const t = perm[i]; perm[i] = perm[k - i]; perm[k - i] = t;
        i += 1;
      }
      k = perm[0];
      f += 1;
    }
    if (f > flips) flips = f;
    if ((nperm & 0x1) === 0) checksum += f; else checksum -= f;

    // Use incremental change to generate another permutation
    let more = true;
    while (more) {
      if (r === n) {
        console.log(checksum);
        return flips;
      }
      const p0 = perm1[0];
      i = 0;
      while (i < r) {
        const j = i + 1;
        perm1[i] = perm1[j];
        i = j;
      }
      perm1[r] = p0;

      count[r] -= 1;
      if (count[r] > 0) more = false; else r += 1;
    }
    nperm += 1;
  }
  return flips;
}

const n: int = process.argv.length > 2 ? Math.trunc(Number(process.argv[2])) : 7;

console.log(`Pfannkuchen(${n}) = ${fannkuch(n)}`);
