// A reversal of a register int[] prefix followed by a read of its first element (fannkuch-redux's flip loop):
// the element is taken from the value before the shuffle (emit.c rev_head), the same as reading after.

function flips(n: int, seed: int): string {
  const perm: int[] = new Array<int>(n).fill(0);
  const work: int[] = new Array<int>(n).fill(0);
  for (let i = 0; i < n; i++) perm[i] = i;
  for (let s = 0; s < seed; s++) { // swap neighbours from s: a mixed start whose values are still the indexes
    const j = s % n, k = (s * 3 + 1) % n;
    const t = perm[j]; perm[j] = perm[k]; perm[k] = t;
  }
  let total = 0, most = 0;
  for (let round = 0; round < n; round++) {
    for (let i = 0; i < n; i++) work[i] = perm[i];
    let k = work[0];
    let f = 0;
    while (k !== 0 && f < 40) {
      for (let i = 0; 2 * i < k; i++) {
        const t = work[i]; work[i] = work[k - i]; work[k - i] = t;
      }
      k = work[0];
      f += 1;
    }
    total += f;
    if (f > most) most = f;
    const first = perm[0];
    for (let i = 0; i < n - 1; i++) perm[i] = perm[i + 1];
    perm[n - 1] = first;
  }
  return `${n}/${seed}: ${total} ${most}`;
}
const sizes: int[] = [1, 2, 5, 7, 12, 16, 17];
for (const n of sizes) console.log(flips(n, 5), flips(n, 7));
