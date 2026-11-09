// wib: the checks of a[i] dropped in `while (... && i < E) { ...; i += 1 }` when a guard before the loop shows
// every i the loop reaches is within a; the loop as written otherwise. And the next-permutation rotation in one
// shuffle when x is known to hold a[0] (a[0] = y; ...; x = y).

// the guard holds: E < length
function clearRun(c: int[], n: int): int {
  let i = 1;
  let steps = 0;
  while (c[i] > i && i < n - 1) {
    c[i] = 0;
    i += 1;
    steps += 1;
  }
  return steps * 100 + i;
}

// the guard fails (E past the length): the first conjunct stops the loop before the end
function stopEarly(c: int[], e: int): int {
  let i = 0;
  while (c[i] !== 0 && i < e) {
    c[i] -= 1;
    i++;
  }
  return i;
}

// E a name and a literal on the left; a start of 0
function sumUpTo(a: int[], k: int): int {
  let i = 0;
  let s: int = 0;
  while (i < 2 + k) {
    s += a[i];
    i += 1;
  }
  return s;
}

// the permutations of 0..n-1 in fannkuch-redux's order (a counter per prefix), each one hashed
function permutations(n: int): string {
  const perm: int[] = new Array<int>(n).fill(0);
  const count: int[] = new Array<int>(n).fill(0);
  for (let i = 0; i < n; i++) perm[i] = i;
  let total: int = 0;
  let hash: int = 0;
  let steps: int = 1;
  for (let k = 1; k <= n; k++) steps *= k;
  for (let s = 0; s < steps; s++) {
    let h: int = 0;
    for (let j = 0; j < n; j++) h = (h * 31 + perm[j] + 1) % 1000003;
    hash = (hash + h * (s + 1)) % 1000003;
    total += 1;
    if (n < 2) continue;
    let first = perm[1];
    perm[1] = perm[0];
    perm[0] = first;
    let i = 1;
    count[i] += 1;
    while (count[i] > i && i < n - 1) {
      count[i] = 0;
      i += 1;
      const next = perm[1];
      perm[0] = next;
      for (let j = 1; j < i; j++) perm[j] = perm[j + 1];
      perm[i] = first;
      first = next;
      count[i] += 1;
    }
  }
  return `${n}: ${total} ${hash}`;
}

// x not known to hold a[0]: the rotation must not be assumed
function notSynced(n: int): string {
  const p: int[] = new Array<int>(n).fill(0);
  for (let i = 0; i < n; i++) p[i] = i;
  let x = p[n - 1];
  let i = 1;
  while (i < n - 1) {
    i += 1;
    const y = p[1];
    p[0] = y;
    for (let j = 1; j < i; j++) p[j] = p[j + 1];
    p[i] = x;
    x = p[1];
  }
  let out = "";
  for (let j = 0; j < n; j++) out += `${j > 0 ? "," : ""}${p[j]}`;
  return out;
}

console.log(clearRun([0, 5, 6, 7, 0, 0], 6), clearRun([0, 9, 9, 9], 4), clearRun([0, 0], 2), clearRun([0, 3, 4], 10 - 9));
console.log(stopEarly([3, 2, 0, 5], 100), stopEarly([1, 1, 1], -5), stopEarly([2, 0], 1));
console.log(sumUpTo([1, 2, 3, 4, 5], 3), sumUpTo([7, 8], 0), sumUpTo([4, 4, 4], -2));
for (let n = 1; n <= 8; n++) console.log(permutations(n));
console.log(permutations(17 - 9));
console.log(notSynced(2), notSynced(5), notSynced(17), notSynced(20));
