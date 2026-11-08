// x % d where d is known to be a power of two (a literal, an entry of a constant table of powers of two, or a
// local set only from one): a mask for x >= 0, JavaScript's sign rule for x < 0; and the range of x % d keeps the
// arithmetic around it unchecked when it cannot overflow
const POW2: int[] = [1, 2, 4, 8, 16, 1024, 65536];
const MIXED: int[] = [1, 3, 4];

function mods(xs: int[], k: int): string {
  const d = POW2[k];
  let s = "";
  for (const x of xs) s += `${x % d} ${(x % d) * 4 + 3} ${x % 8} `;
  return s;
}

function notPow2(xs: int[], k: int): string {
  const d = MIXED[k];
  let s = "";
  for (const x of xs) s += `${x % d} `;
  return s;
}

const xs: int[] = [0, 1, 7, 8, 9, 1023, 1024, 65535, 65537, -1, -7, -8, -9, -1024, -65537, 9007199254740991, -9007199254740991];
for (let k = 0; k < POW2.length; k++) console.log(k, mods(xs, k));
for (let k = 0; k < MIXED.length; k++) console.log(k, notPow2(xs, k));
let key: int = 12345;
for (let i = 0; i < 20; i++) key = (key % POW2[5]) * 4 + (i & 3);
console.log(key);
