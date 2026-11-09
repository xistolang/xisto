// reverse of xisto:bytes over several MB with a kept character: when every line has the first line's width (the
// last one that or shorter), xisto proves it while it counts the kept bytes and places the pairs by arithmetic
// (xs_u8_rev_par); one line out of place anywhere sends it back to scanning. The same bytes as Node either way:
// regular lines, a short last line, a range ending on a line break, width 1, one longer and one shorter line in
// the same block, one extra line break, and a range that starts in the middle of a line
import { byteMap, reverse } from "xisto:bytes";

function checksum(b: Uint8Array): int {
  let h: int = 0;
  for (let i = 0; i < b.length; i++) h = (h * 31 + b[i]) % 1000000007;
  return h;
}

// n bytes of lines `width` wide; at `odd` (a byte index, -1: none) one line is `delta` longer
function fixed(n: int, width: int, odd: int, delta: int, seed: int): Uint8Array {
  const out = new Uint8Array(n);
  let s: int = seed;
  let col: int = 0;
  let w: int = width;
  for (let i = 0; i < n; i++) {
    s = (s * 3877 + 29573) % 139968;
    if (col === w) {
      out[i] = 10;
      col = 0;
      w = i >= odd && odd >= 0 && i < odd + 3 * width ? width + delta : width;
    } else {
      out[i] = Math.trunc("ACGTNacgt".charCodeAt(s % 9));
      col++;
    }
  }
  return out;
}

const COMPLEMENT = byteMap("ACGTUMRWSYKVHDBN", "TGCAAKYWSRMBDHVN", { ignoreCase: true });
const N: int = 3000017;
const cases: int[][] = [
  [60, -1, 0], [60, 1500000, 1], [60, 1500000, -1], [60, 400000, 7], [1, -1, 0], [97, -1, 0], [61, 2999000, -30],
];
for (const c of cases) {
  const a = fixed(N, c[0], c[1], c[2], c[0] + 13);
  reverse(a, { map: COMPLEMENT, skip: "\n" });
  console.log("width", c[0], "odd", c[1], c[2], checksum(a));
}
// the range ends on a line break, and one that starts in the middle of a line
const b = fixed(N, 60, -1, 0, 5);
let end: int = N;
while (b[end - 1] !== 10) end--;
reverse(b, { map: COMPLEMENT, skip: "\n", to: end });
console.log("ends on break", checksum(b));
const d = fixed(N, 60, -1, 0, 9);
reverse(d, { map: COMPLEMENT, skip: "\n", from: 30 });
console.log("starts mid-line", checksum(d));
// an extra line break inside a line
const e = fixed(N, 60, -1, 0, 21);
e[1234567] = 10;
reverse(e, { map: COMPLEMENT, skip: "\n" });
console.log("extra break", checksum(e));
