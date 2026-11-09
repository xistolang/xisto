// a loop whose body declares the array it indexes (a for...of variable, a const): the bounds check stays inside,
// where the name exists (the guard before the loop cannot read it)
const seen: int[][] = [[1, 0, 0], [0, 1, 0], [0, 0, 0]];
for (let b = 0; b < 3; b++) {
  let any = false;
  for (const s of seen) if (s[b] === 1) any = true;
  console.log(b, any);
}
const rows: int[][] = [[5, 6, 7], [8, 9, 10]];
for (let i = 0; i < 3; i++) {
  const r = rows[i % 2];
  const off = 1;
  console.log(r[i], r[i + 0], r[2 - i], r[i] + off);
}
