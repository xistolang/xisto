// small local Uint8Arrays a loop only indexes live in a vector register for the loop (emit.c u8reg): reads and
// writes by variable index, the reversal, copy and shift idioms, and the ways out of the rule (escape to a
// function, .length, a[i]++), all with the same answers as the Node program
function flips(n: int, seed: int): int {
  const p = new Uint8Array(16);
  const pp = new Uint8Array(16);
  for (let i = 0; i < n; i++) p[i] = (i * seed + 3) % n;
  let total: int = 0;
  for (let round = 0; round < 50; round++) {
    for (let j = 0; j < n; j++) pp[j] = p[j];
    let p0 = pp[0];
    let count: int = 0;
    while (p0 !== 0 && count < 100) {
      const next = pp[p0];
      count++;
      for (let i = 1; i < (p0 + 1) >> 1; i++) {
        const t = pp[i];
        pp[i] = pp[p0 - i];
        pp[p0 - i] = t;
      }
      pp[p0] = p0;
      if (next === p0) break;
      p0 = next;
    }
    total = (total + count) % 1000003;
    // next permutation: rotate the first k + 2 elements left (k + 1 < n)
    const k = (round % (n - 2)) + 1;
    const first = p[0];
    for (let j = 0; j <= k; j++) p[j] = p[j + 1];
    p[k + 1] = first;
    if (p[0] === p[1]) p[1] = p[2];
  }
  let sum: int = 0;
  for (let i = 0; i < n; i++) sum = (sum * 31 + p[i] + pp[i]) % 1000003;
  return total * 1000003 + sum;
}
// smaller than 16: loads and stores only its bytes
function small(): string {
  const a = new Uint8Array(5);
  for (let r = 0; r < 7; r++) {
    a[r % 5] = r * 40;
    const i = (r * 3) % 5;
    a[i] = a[i] + a[(i + 1) % 5];
    for (let j = 0; j < 4; j++) a[j] = a[j + 1];
  }
  return `${a[0]},${a[1]},${a[2]},${a[3]},${a[4]}`;
}
// escapes: passed to a function, measured, captured by an arrow, incremented: stays in memory, same answers
function sumOf(b: Uint8Array): int { let s: int = 0; for (let i = 0; i < b.length; i++) s += b[i]; return s; }
function escapes(): string {
  const a = new Uint8Array(8);
  const b = new Uint8Array(8);
  const c = new Uint8Array(8);
  let acc: int = 0;
  for (let r = 0; r < 9; r++) {
    a[r % 8] = r;
    acc += sumOf(a);
    b[(r * 5) % 8] = r;
    acc += b.length;
    c[r % 8]++;
    c[(r + 1) % 8] += 2;
  }
  const f = () => a[2] + c[3];
  return `${acc} ${f()} ${b[5]} ${c[0]}`;
}
console.log(flips(12, 7), flips(9, 2), flips(15, 5), flips(3, 1));
console.log(small());
console.log(escapes());
