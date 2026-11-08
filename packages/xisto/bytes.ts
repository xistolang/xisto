// xisto:bytes: in-place operations on byte arrays.
// This file is both: the module the xisto compiler builds in (the loops are written in the shapes it turns into
// one vector call: xs_u8_rev, xs_u8_trade), and the reference implementation Node runs, with the same output.

// Reverses a[start..end) in place.
export function reverse(a: Uint8Array, start: int = 0, end: int = a.length): void {
  const s = start + end - 1; // a[i] trades places with a[s - i]
  for (let i = start; i < (s + 1) >> 1; i++) {
    const t = a[i];
    a[i] = a[s - i];
    a[s - i] = t;
  }
}

// a[i] = table[a[i]] for i in [start, end).
export function translate(a: Uint8Array, table: Uint8Array, start: int = 0, end: int = a.length): void {
  for (let i = start; i < end; i++) a[i] = table[a[i]];
}

// Reverses a[start..end) and translates every byte through table, in one pass.
export function reverseTranslate(a: Uint8Array, table: Uint8Array, start: int = 0, end: int = a.length): void {
  const n = end - start;
  const half = n >> 1;
  const last = end - 1;
  for (let k = 0; k < half; k++) {
    const x = a[start + k];
    a[start + k] = table[a[last - k]];
    a[last - k] = table[x];
  }
  if ((n & 1) === 1) {
    const m = start + half;
    a[m] = table[a[m]];
  }
}

// A table of 256 bytes: every byte to itself, except from[k] to to[k] (letters in both cases when ignoreCase).
export function byteTable(from: string, to: string, ignoreCase: boolean = false): Uint8Array {
  const t = new Uint8Array(256);
  for (let i = 0; i < 256; i++) t[i] = i;
  for (let k = 0; k < from.length; k++) {
    const f = Math.trunc(from.charCodeAt(k)), v = Math.trunc(to.charCodeAt(k)); // (a char code is an int)
    t[f] = v;
    if (ignoreCase) {
      t[f | 0x20] = v;
      t[f & ~0x20] = v;
    }
  }
  return t;
}

// Reverses and translates the bytes of a[start..end) that are not `keep`, leaving every `keep` byte where it is:
// the reverse complement of a sequence written in lines (keep = '\n') keeps its line layout.
export function reverseTranslateKeeping(a: Uint8Array, table: Uint8Array, keep: int, start: int = 0, end: int = a.length): void {
  let i = start; // the front cursor, and the end of its stretch (the next `keep`, or end)
  let iEnd = stretchEnd(a, keep, i, end);
  let j = end - 1; // the back cursor, and the start of its stretch
  while (j >= start && a[j] === keep) j--;
  let jStart = a.lastIndexOf(keep, j) + 1;
  if (jStart < start) jStart = start;
  while (i < j) {
    if (i === iEnd) { // past a `keep`: the next stretch
      i++;
      iEnd = stretchEnd(a, keep, i, end);
      continue;
    }
    if (j < jStart) {
      j--;
      jStart = a.lastIndexOf(keep, j) + 1;
      if (jStart < start) jStart = start;
      continue;
    }
    // a run in which neither cursor leaves its stretch (and they do not cross)
    let run = iEnd - i;
    const room = j - jStart + 1;
    if (room < run) run = room;
    const meet = (j - i + 1) >> 1;
    if (meet < run) run = meet;
    if (run === 0) break;
    for (let t = 0; t < run; t++) {
      const x = a[i + t];
      a[i + t] = table[a[j - t]];
      a[j - t] = table[x];
    }
    i += run;
    j -= run;
  }
  if (i === j && a[i] !== keep) a[i] = table[a[i]]; // the middle byte
}

function stretchEnd(a: Uint8Array, keep: int, from: int, end: int): int {
  const q = a.indexOf(keep, from);
  return q < 0 || q > end ? end : q;
}
