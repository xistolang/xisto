// reverse of xisto:bytes with a kept character on ranges from 4 KB (xisto turns them in C on one thread when the
// pairs are not split between threads, xs_rap_seq): the same bytes as Node, with lines of every width (1 byte,
// empty lines, a kept byte at either end), odd and even counts, and the range in the middle of the buffer
import { byteMap, reverse } from "xisto:bytes";

const COMPLEMENT = byteMap("ACGTN", "TGCAN", { ignoreCase: true });

function checksum(b: Uint8Array): int {
  let h: int = 0;
  for (let i = 0; i < b.length; i++) h = (h * 31 + b[i]) % 1000000007;
  return h;
}

function text(n: int, seed: int, widest: int): Uint8Array {
  const out = new Uint8Array(n);
  let s: int = seed;
  let col: int = 0;
  let width: int = 1 + (seed % widest);
  for (let i = 0; i < n; i++) {
    s = (s * 3877 + 29573) % 139968;
    if (col >= width) {
      out[i] = 10;
      col = 0;
      width = s % (widest + 1); // (0: an empty line, two newlines in a row)
    } else {
      out[i] = Math.trunc("ACGTNacgt".charCodeAt(s % 9));
      col++;
    }
  }
  return out;
}

const sizes: int[] = [4096, 4097, 5000, 65536, 99999, 300001];
const widths: int[] = [1, 3, 16, 60, 200];
for (const n of sizes) {
  for (const w of widths) {
    const b = text(n, n + w, w);
    b[0] = 10; // a kept byte at the start of the buffer
    reverse(b, { map: COMPLEMENT, skip: "\n" });
    const c = text(n, n * 7 + w, w);
    c[n - 1] = 10; // and at the end
    reverse(c, { map: COMPLEMENT, skip: "\n", from: 7, to: n - 5 });
    console.log(n, w, checksum(b), checksum(c), b[0], b[n - 1], c[3], c[n - 2]);
  }
}
