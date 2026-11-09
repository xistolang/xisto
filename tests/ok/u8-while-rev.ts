// the reversal written with while over a byte array (fannkuch's first program): one call, same result
function flip(a: Uint8Array, k: int): int {
  let i = 0;
  while (2 * i < k) {
    const t = a[i];
    a[i] = a[k - i];
    a[k - i] = t;
    i += 1;
  }
  return i;
}
function flipFrom(a: Uint8Array, start: int, k: int): int {
  let i = start;
  while (i * 2 < k) {
    const t = a[i];
    a[i] = a[k - i];
    a[k - i] = t;
    i++;
  }
  return i;
}
const a = new Uint8Array(12);
for (let n = 0; n < 12; n++) a[n] = n * 3;
for (let k = 0; k < 12; k++) {
  const b = a.slice();
  const i = flip(b, k);
  console.log(k, i, b.join(","));
}
const c = a.slice();
console.log(flipFrom(c, 2, 9), c.join(","));
console.log(flipFrom(c, 7, 9), c.join(","));
