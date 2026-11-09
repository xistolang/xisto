// xisto:bytes and xisto:io: the same results as their reference implementations in Node
import { byteMap, indexOf, lastIndexOf, mapBytes, reverse } from "xisto:bytes";
import { writeStdout } from "xisto:io";

const COMPLEMENT = byteMap("ACGTUMRWSYKVHDBN", "TGCAAKYWSRMBDHVN", { ignoreCase: true });
const enc = new TextEncoder(), dec = new TextDecoder();
const show = (a: Uint8Array): string => JSON.stringify(dec.decode(a));

// a small generator whose numbers JavaScript keeps exact
let seed = 12345;
function next(): int {
  seed = (seed * 1103 + 12345) % 65536;
  return seed >> 8;
}

// reverse: every length up to 40, from every start, against Array's reverse
for (let n = 0; n <= 40; n += 7) {
  for (let s = 0; s <= n; s += 3) {
    const a = new Uint8Array(n);
    for (let i = 0; i < n; i++) a[i] = next();
    const b = a.slice();
    reverse(a, { from: s });
    const c = b.subarray(s, n).slice().reverse();
    let same = true;
    for (let i = 0; i < n - s; i++) if (a[s + i] !== c[i]) same = false;
    if (!same) console.log("reverse differs", n, s);
  }
}
console.log("reverse ok");

// reverse with map, against mapBytes and then reverse
const letters = "ACGTacgtNnMRWSYKVHDBUx\n";
for (let n = 0; n <= 70; n += 9) {
  const a = new Uint8Array(n);
  for (let i = 0; i < n; i++) a[i] = Math.trunc(letters.charCodeAt(next() % letters.length));
  const b = a.slice(), c = a.slice();
  reverse(b, { map: COMPLEMENT });
  mapBytes(c, COMPLEMENT);
  reverse(c);
  console.log(n, show(b), show(c));
}
const part = enc.encode("acgtACGT");
mapBytes(part, COMPLEMENT, { from: 2, to: 6 });
reverse(part, { to: 4 });
console.log(show(part));

// reverse with skip: the line breaks stay where they are
const k = enc.encode(">one\nACGTACGTAC\nGGTTAACCGG\nAC\n>two\nNNacgt\n");
reverse(k, { map: COMPLEMENT, skip: "\n", from: 5, to: 31 });
reverse(k, { map: COMPLEMENT, skip: "\n", from: 36 });
writeStdout(k);
const plain = enc.encode("ab\ncd\ne");
reverse(plain, { skip: "\n" });
console.log(show(plain));

// long stretches: the 8-byte search of lastIndexOf
const long = new Uint8Array(1000);
for (let i = 0; i < long.length; i++) long[i] = i % 61 === 60 ? 10 : Math.trunc("ACGT".charCodeAt(next() % 4));
const l2 = long.slice();
reverse(long, { map: COMPLEMENT, skip: "\n" });
function digest(a: Uint8Array): int {
  let sum: int = 0;
  for (let i = 0; i < a.length; i++) sum = (sum * 31 + a[i]) % 1000003;
  return sum;
}
console.log(digest(long), lastIndexOf(long, "\n"), lastIndexOf(long, "\n", { from: 500 }), indexOf(l2, "\n"));
console.log(indexOf(l2, ">"), indexOf(l2, "A", { from: 990 }), lastIndexOf(l2, "C", { from: 3 }));

// a character must be one ASCII character, and a map 256 bytes
for (const bad of ["", "ab", "é"]) {
  try { indexOf(l2, bad); } catch (e) { console.log((e as Error).message); }
}
try { mapBytes(l2, new Uint8Array(4)); } catch (e) { console.log((e as Error).message); }
try { byteMap("ab", "c"); } catch (e) { console.log((e as Error).message); }
