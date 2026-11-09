// Arrays that only hold indices of themselves (ix_prove) and fixed local arrays read through C locals
// (fix-hoist): the flip counts of every permutation, with arrays of a runtime length; and cases the rules must
// refuse (a value from outside, a length that differs), which keep their checks and give the same output.
const n: int = process.argv.length > 99 ? 0 : 7;

function flips(start: int): int {
  const perm1: int[] = new Array<int>(n).fill(0);
  const perm: int[] = new Array<int>(n).fill(0);
  const count: int[] = new Array<int>(n).fill(0);
  for (let i = 0; i < n; i++) perm1[i] = i;
  // rotate by start
  for (let r = 0; r < start; r++) {
    const first = perm1[0];
    for (let j = 0; j < n - 1; j++) perm1[j] = perm1[j + 1];
    perm1[n - 1] = first;
  }
  let total = 0, most = 0;
  for (let index = 0; index < 5040; index++) {
    if (perm1[0] !== 0) {
      for (let i = 0; i < n; i++) perm[i] = perm1[i];
      let f = 0;
      let k = perm[0];
      while (k !== 0) {
        for (let i = 0; 2 * i < k; i++) {
          const t = perm[i]; perm[i] = perm[k - i]; perm[k - i] = t;
        }
        k = perm[0];
        f += 1;
      }
      total += f;
      if (f > most) most = f;
    }
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
  return total * 100 + most;
}

// a value from outside: the array is not one of index values (the reversal keeps its check)
function outside(seed: int): int {
  const a: int[] = new Array<int>(n).fill(0);
  for (let i = 0; i < n; i++) a[i] = (i * seed) % n;
  let k = a[1];
  let s = 0;
  while (k !== 0 && s < 50) {
    for (let i = 0; 2 * i < k; i++) {
      const t = a[i]; a[i] = a[k - i]; a[k - i] = t;
    }
    k = a[0];
    s += 1;
  }
  return s * 1000 + a[0] * 10 + a[n - 1];
}

// two lengths: only the arrays of one length are of index values
function lengths(): int {
  const a: int[] = new Array<int>(n).fill(0);
  const b: int[] = new Array<int>(3).fill(0);
  for (let i = 0; i < n; i++) a[i] = n - 1 - i;
  for (let i = 0; i < 3; i++) b[i] = a[i] % 3;
  let k = a[0];
  for (let i = 0; 2 * i < k; i++) {
    const t = a[i]; a[i] = a[k - i]; a[k - i] = t;
  }
  return a[0] * 100 + a[n - 1] * 10 + b[2];
}

// a fixed array of a runtime length, read and written by computed indices
function hist(m: int): string {
  const h: int[] = new Array<int>(m).fill(0);
  for (let i = 0; i < 1000; i++) h[(i * 7 + 3) % m] += 1;
  let out = "";
  for (let i = 0; i < m; i++) out += `${h[i]} `;
  return out;
}

for (let s = 0; s < 3; s++) console.log(`flips ${s}: ${flips(s)}`);
for (let s = 1; s < 6; s++) console.log(`outside ${s}: ${outside(s)}`);
console.log(`lengths: ${lengths()}`);
console.log(hist(5));
console.log(hist(13));
