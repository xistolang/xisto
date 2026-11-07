// integer ranges (emit.c): a + b, a - b, a * b lose their overflow check only when they provably cannot
// overflow; every case where they can must still report it (and be catchable), as before. Checked against the
// output without the analysis (XISTO_NO_RANGES=1): int overflow is an error in xisto, not in Node.
const big = new Uint8Array(1000);
let s: int = 0;
for (let i: int = 0; i < big.length; i++) {
  big[i] = (i * 31 + 7) & 255;
  s = (s + big[i] * ((i >> 3) & 15)) & 0xffff;
}
console.log("safe", s, big[999]);

// a counter that is written in the body keeps its checks
let n: int = 0;
for (let i: int = 0; i < 10; i++) {
  n += i;
  if (i === 5) {
    i = 8;
  }
}
console.log("written counter", n);

// a variable that grows without bound: checked, and the overflow is caught
let x: int = 1;
try {
  for (let i: int = 0; i < 100; i++) {
    x = x * 3;
  }
  console.log("no overflow?", x);
} catch (e) {
  console.log("caught:", e.message);
}

// a masked value times a large constant can overflow: checked
let m: int = 0x7fffffff & 123456789;
try {
  const y: int = m * 9223372036;
  console.log("y", y);
} catch (e) {
  console.log("caught:", e.message);
}

// a length times a constant fits; a length times a length may not (bounded by 2^40 each: 2^80)
const arr: int[] = [1, 2, 3];
let fits: int = arr.length * 1000000;
console.log("fits", fits);

// a closure that writes the variable: no range
let c: int = 0;
const bump = (): void => {
  c = c + 1000000000000;
};
for (let i: int = 0; i < 3; i++) {
  bump();
}
console.log("closure", c + 1);

// negative ranges and subtraction
let d: int = 0;
for (let i: int = 0; i < 50; i++) {
  d = (i % 7) - 10;
}
console.log("neg", d * 1000 - 5);
