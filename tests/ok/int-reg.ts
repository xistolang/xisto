// int[] arrays that only ever hold bytes, kept in a vector register by the loops that use them (emit.c u8reg,
// bv_prove): the same results as in memory, at any length (over 16 the loop runs in memory).

function flips(n: int, rounds: int): string {
  const perm1: int[] = new Array<int>(n).fill(0);
  const perm: int[] = new Array<int>(n).fill(0);
  for (let i = 0; i < n; i++) perm1[i] = i; // the store's own index: below the length
  let total = 0, most = 0;
  for (let r = 0; r < rounds; r++) {
    for (let i = 0; i < n; i++) perm[i] = perm1[i];
    let k = perm[0];
    let f = 0;
    while (k !== 0) {
      for (let i = 0; 2 * i < k; i++) {
        const t = perm[i]; perm[i] = perm[k - i]; perm[k - i] = t;
      }
      k = perm[0];
      f += 1;
    }
    total += f;
    if (f > most) most = f;
    // rotate the whole permutation left by one, then swap a pair picked by r
    const first = perm1[0];
    for (let j = 0; j < n - 1; j++) perm1[j] = perm1[j + 1];
    perm1[n - 1] = first;
    const a = r % n, b = (r * 7 + 3) % n;
    const t = perm1[a]; perm1[a] = perm1[b]; perm1[b] = t;
  }
  return `${n}: ${total} ${most} [${perm1.join(",")}]`;
}

const sizes: int[] = [1, 2, 5, 9, 16, 17, 23];
for (const n of sizes) console.log(flips(n, 200));

// a value that is not a byte: this one stays in memory (300 does not fit a lane)
function wide(n: int): string {
  const a: int[] = new Array<int>(n).fill(0);
  for (let r = 0; r < 3; r++) for (let i = 0; i < n; i++) a[i] = i * 100 + r;
  return a.join(",");
}
console.log(wide(5));
