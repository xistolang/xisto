// reverse of xisto:bytes over ranges of several MB (on 4 cores or more xisto splits the pairs between threads,
// xs_u8_rev_par): the same bytes as the one-thread loop and as Node, with and without a kept character, odd and
// even counts of moved bytes, lines of different lengths, and a map that sends a byte to the kept one (one thread)
import { byteMap, reverse } from "xisto:bytes";

function checksum(b: Uint8Array): int {
  let h: int = 0;
  for (let i = 0; i < b.length; i++) h = (h * 31 + b[i]) % 1000000007;
  return h;
}

function lines(n: int, seed: int): Uint8Array {
  const out = new Uint8Array(n);
  let s: int = seed;
  let col: int = 0;
  let width: int = 60;
  for (let i = 0; i < n; i++) {
    s = (s * 3877 + 29573) % 139968;
    if (col === width) {
      out[i] = 10;
      col = 0;
      width = 1 + (s % 97);
    } else {
      out[i] = Math.trunc("ACGTNacgt".charCodeAt(s % 9));
      col++;
    }
  }
  return out;
}

const COMPLEMENT = byteMap("ACGTUMRWSYKVHDBN", "TGCAAKYWSRMBDHVN", { ignoreCase: true });
const SIZES: int[] = [3000001, 3000000, 2500007];
for (const n of SIZES) {
  const a = lines(n, n);
  reverse(a, { map: COMPLEMENT, skip: "\n", from: 5, to: n - 3 });
  console.log(n, "around", checksum(a), a[0], a[n - 1]);
  const b = lines(n, 7);
  reverse(b, { map: COMPLEMENT, from: 1 });
  console.log(n, "mapped", checksum(b));
  const c = lines(n, 11);
  reverse(c, { skip: "\n" });
  console.log(n, "plain around", checksum(c));
}
// a map that writes the kept byte: Node's loop sees the new line breaks; xisto keeps to its one-thread loop
const odd = lines(2000003, 3);
reverse(odd, { map: byteMap("A", "\n"), skip: "\n" });
console.log("to keep", checksum(odd));
