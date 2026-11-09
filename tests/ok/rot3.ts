// `a[L - 1] = next; for (j = L; j < i; j++) a[j] = a[j + 1]; a[i] = x;` on a small int[] (in a register): one shuffle
// and one lane when the shift runs (i >= L), the statements as written otherwise. Same as Node for every i.

function rotate(n: int, i: int, x: int): string {
  const a: int[] = new Array<int>(n).fill(0);
  for (let j = 0; j < n; j++) a[j] = 10 + j;
  let out = "";
  for (let r = 0; r < 3; r++) {
    const next = a[1];
    a[0] = next;
    for (let j = 1; j < i; j++) a[j] = a[j + 1];
    a[i] = x;
    for (let j = 0; j < n; j++) out += `${a[j]} `;
    out += "| ";
  }
  return out;
}

// the next permutation of fannkuch-redux as one writes it first: first holds a[0] when the rotation starts
function permutations(n: int, steps: int): string {
  const p: int[] = new Array<int>(n).fill(0);
  const count: int[] = new Array<int>(n).fill(0);
  for (let i = 0; i < n; i++) p[i] = i;
  let out = "";
  for (let s = 0; s < steps; s++) {
    let first = p[1];
    p[1] = p[0];
    p[0] = first;
    let i = 1;
    count[i] += 1;
    while (count[i] > i && i < n - 1) {
      count[i] = 0;
      i += 1;
      const next = p[1];
      p[0] = next;
      for (let j = 1; j < i; j++) p[j] = p[j + 1];
      p[i] = first;
      first = next;
      count[i] += 1;
    }
    let row = "";
    for (let j = 0; j < n; j++) row += `${p[j]}`;
    out += row + " ";
  }
  return out;
}

console.log(rotate(6, 4, 99));
console.log(rotate(6, 1, 77));
console.log(rotate(6, 2, 55));
console.log(rotate(5, 0, 33));
console.log(permutations(4, 24));
console.log(permutations(6, 40));
