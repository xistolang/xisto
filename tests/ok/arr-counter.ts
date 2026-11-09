// Counters in a local int[] (XB-089): `new Array<int>(N).fill(K)` then the one counted loop that only adds `X[e]++`
// to it. The increments go without the range test, and one test on K + c * (B - A) after the loop covers them.
// Also: a local int[] of a known length indexed by a masked key needs no index check; and a key whose definitions
// copy each other (`key = start`, `start = key`) still gets its range.
function histogram(bytes: Uint8Array, lo: int, hi: int): int[] {
  const tally: int[] = new Array<int>(16).fill(0);
  let key: int = 0;
  const start = key;
  for (let j = lo; j < hi; j++) {
    key = ((key << 2) | (bytes[j] & 3)) & 15;
    tally[key]++;
    if (bytes[j] === 0) tally[0]++; // two increments in the body (c = 2)
  }
  key = start;
  const out: int[] = [];
  for (let k = 0; k < 16; k++) out.push(tally[k] + key);
  return out;
}
const data = new Uint8Array(1000);
for (let i = 0; i < data.length; i++) data[i] = (i * 7 + 3) % 11;
console.log(histogram(data, 0, data.length).join(" "));
console.log(histogram(data, 10, 5).join(" ")); // a loop that never runs
console.log(histogram(data, 999, 1000).join(" "));

// not the rule: the array read inside the loop (the value matters before the loop ends)
function running(bytes: Uint8Array, n: int): int {
  const seen: int[] = new Array<int>(4).fill(0);
  let s = 0;
  for (let j = 0; j < n; j++) {
    seen[bytes[j] & 3]++;
    s += seen[bytes[j] & 3];
  }
  return s;
}
console.log(running(data, data.length));

// not the rule: written outside the loop too
function twice(bytes: Uint8Array, n: int): int {
  const c: int[] = new Array<int>(4).fill(1);
  c[2] = 7;
  for (let j = 0; j < n; j++) c[bytes[j] & 3]++;
  return c[0] + c[1] + c[2] + c[3];
}
console.log(twice(data, data.length));

// a large starting value: still exact (the bound test after the loop finds nothing past the range)
function big(n: int): int {
  const t: int[] = new Array<int>(2).fill(9007199254740900);
  for (let j = 0; j < n; j++) t[j & 1]++;
  return t[0] - 9007199254740900 + t[1] - 9007199254740900;
}
console.log(big(90), big(0));
