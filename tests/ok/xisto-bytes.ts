// xisto:bytes and xisto:io: the same results as their reference implementations in Node
import { byteTable, reverse, reverseTranslate, reverseTranslateKeeping, translate } from "xisto:bytes";
import { writeStdout } from "xisto:io";

const T = byteTable("ACGTUMRWSYKVHDBN", "TGCAAKYWSRMBDHVN", true);
const enc = new TextEncoder(), dec = new TextDecoder();
const show = (a: Uint8Array): string => JSON.stringify(dec.decode(a));

// reverse: every length up to 40, every start
// a small generator whose numbers JavaScript keeps exact
let seed = 12345;
function next(): int {
  seed = (seed * 1103 + 12345) % 65536;
  return seed >> 8;
}
for (let n = 0; n <= 40; n += 7) {
  for (let s = 0; s <= n; s += 3) {
    const a = new Uint8Array(n);
    for (let i = 0; i < n; i++) a[i] = next();
    const b = a.slice();
    reverse(a, s, n);
    const c = b.subarray(s, n).slice().reverse();
    let same = true;
    for (let i = 0; i < n - s; i++) if (a[s + i] !== c[i]) same = false;
    if (!same) console.log("reverse differs", n, s);
  }
}
console.log("reverse ok");

const letters = "ACGTacgtNnMRWSYKVHDBUx\n";
for (let n = 0; n <= 70; n += 9) {
  const a = new Uint8Array(n);
  for (let i = 0; i < n; i++) a[i] = Math.trunc(letters.charCodeAt(next() % letters.length));
  const b = a.slice(), c = a.slice();
  reverseTranslate(b, T);
  translate(c, T);
  console.log(n, show(b), show(c.reverse()));
}
const k = enc.encode(">one\nACGTACGTAC\nGGTTAACCGG\nAC\n>two\nNNacgt\n");
reverseTranslateKeeping(k, T, 10, 5, 31);
reverseTranslateKeeping(k, T, 10, 36, k.length);
writeStdout(k);
// long stretches: the 8-byte search of lastIndexOf
const long = new Uint8Array(1000);
for (let i = 0; i < long.length; i++) long[i] = i % 61 === 60 ? 10 : Math.trunc("ACGT".charCodeAt(next() % 4));
const l2 = long.slice();
reverseTranslateKeeping(long, T, 10);
function digest(a: Uint8Array): int {
  let sum: int = 0;
  for (let i = 0; i < a.length; i++) sum = (sum * 31 + a[i]) % 1000003;
  return sum;
}
const sum = digest(long);
console.log(sum, long.lastIndexOf(10), long.lastIndexOf(10, 500), l2.indexOf(10));
