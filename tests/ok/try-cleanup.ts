// XB-021: an exception releases what the scopes and frames it leaves behind held (the cleanup stack):
// a for-of's iterable, a loop variable, a caller's local array and string. Must end with 0 objects alive.
function check(n: int): int {
  if (n === 3) {
    throw new Error("three");
  }
  return n;
}
function* nums(): Generator<int> {
  for (let i: int = 0; i < 5; i++) {
    yield i;
  }
}
let total: int = 0;
for (let r: int = 0; r < 3; r++) {
  try {
    for (const s of ["a", "bb", "ccc", "dddd"]) {
      total += check(s.length);
    }
  } catch (e) {
    total += 100;
  }
  try {
    for (const n of nums()) {
      total += check(n);
    }
  } catch (e) {
    total += 1000;
  }
}
console.log(total);
function boom(n: int): int {
  if (n > 2) {
    throw new Error("boom");
  }
  return n;
}
function middle(k: int): int {
  const local: int[] = [k, k + 1, k + 2];
  const name: string = `n${k}`;
  return boom(local[2]) + name.length;
}
let ok2: int = 0;
for (let i: int = 0; i < 5; i++) {
  try {
    ok2 += middle(i);
  } catch (e) {
    ok2 += 100;
  }
}
console.log(ok2);
