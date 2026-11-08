// the two-cursor table trade (reverse-complement's inner loop) as one runtime call: runs of every length 0..70,
// a table that is "letters by 5 bits" (SIMD) and one that is not (scalar), bytes >= 0x80, cursors near the
// buffer's ends (the scalar fallback), a table built in a function, a table parameter; XISTO_NO_U8_TRADE=1
// must print the same
function complementTable(): Uint8Array {
  const t = new Uint8Array(256);
  for (let i = 0; i < 256; i++) t[i] = i;
  const pairs = new TextEncoder().encode("ATCGGCTAUAMKRYWWSSYRKMVBHDDHBVNN");
  for (let i = 0; i < pairs.length; i += 2) {
    t[pairs[i]] = pairs[i + 1];
    t[pairs[i] + 32] = pairs[i + 1];
  }
  return t;
}
const table = complementTable();
const odd = new Uint8Array(256); // not "5 bits": a digit maps elsewhere, a letter pair differs by case
for (let i = 0; i < 256; i++) odd[i] = 255 - i;

function fill(buf: Uint8Array, seed: int): void {
  let s = seed + 1;
  const alphabet = new TextEncoder().encode("ACGTacgtNnRYKM>\n@1*xyz");
  for (let i = 0; i < buf.length; i++) {
    s = (s * 48271) % 2147483647; // (stays below 2^53: the same numbers in Node and in xisto)
    const r = (s >> 8) & 0xff;
    buf[i] = r < 200 ? alphabet[r % alphabet.length] : r; // some bytes >= 0x80 too
  }
}

function trade(buf: Uint8Array, T: Uint8Array, a: int, b: int, run: int): void {
  for (let t = 0; t < run; t++) {
    const x = buf[a + t];
    buf[a + t] = T[buf[b - t]];
    buf[b - t] = T[x];
  }
}
// the same, written so the emitter does not fuse it (the reference)
function tradeRef(buf: Uint8Array, T: Uint8Array, a: int, b: int, run: int): void {
  let t = 0;
  while (t < run) {
    const x = buf[a + t];
    const y = buf[b - t];
    buf[a + t] = T[y];
    buf[b - t] = T[x];
    t++;
  }
}
function same(x: Uint8Array, y: Uint8Array): boolean {
  if (x.length !== y.length) return false;
  for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return false;
  return true;
}
function hash(x: Uint8Array): int {
  let h = 7;
  for (let i = 0; i < x.length; i++) h = (h * 31 + x[i]) & 0xffffff;
  return h;
}

let ok = 0, bad = 0, acc = 0;
const buf = new Uint8Array(400);
const ref = new Uint8Array(400);
for (let run = 0; run <= 70; run++) {
  for (let k = 0; k < 4; k++) {
    fill(buf, run * 7 + k);
    ref.set(buf);
    // in the middle (SIMD), symmetric and asymmetric starts; and tight against the ends (scalar fallback)
    const a = k === 2 ? 0 : 100 + k * 3;
    const b = k === 3 ? 399 : 300 - k * 5;
    const T = k === 1 ? odd : table;
    trade(buf, T, a, b, run);
    tradeRef(ref, T, a, b, run);
    if (same(buf, ref)) ok++; else bad++;
    acc = (acc * 31 + hash(buf)) & 0xffffff;
  }
}
console.log(ok, bad, acc);
// overlapping runs (b - a + 1 < 2 * run would cross: the idiom's blocks only overlap exactly in the middle)
fill(buf, 99); ref.set(buf);
trade(buf, table, 50, 109, 30); tradeRef(ref, table, 50, 109, 30);
trade(buf, table, 10, 69, 25); tradeRef(ref, table, 10, 69, 25);
console.log(same(buf, ref), hash(buf));
// a zero-length run and a one-byte run
trade(buf, table, 5, 5, 0); tradeRef(ref, table, 5, 5, 0);
trade(buf, table, 5, 5, 1); tradeRef(ref, table, 5, 5, 1);
console.log(same(buf, ref), buf[5] === table[ref[5]] || buf[5] === ref[5]);
