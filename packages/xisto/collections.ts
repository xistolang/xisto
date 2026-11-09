/**
 * Counting things: how many times each value occurs, and how many times each substring of a text occurs.
 *
 * {@link Counter} is a `Map` from a value to how many times it was added, with the operations a count needs
 * (`add`, `get` that answers 0 for what never came, `mostCommon`). {@link countSubstrings} fills one counter per
 * length with every substring of a text, the job of a k-mer count over a DNA sequence, of n-gram statistics, or
 * of finding repeated pieces in a log.
 *
 * @remarks
 * Compatibility: this file is the module itself. In Node it runs as the reference implementation, with
 * `node --import ./packages/xisto/register.mjs program.ts`: the same counts, in the same order, on one thread.
 * The xisto compiler builds the same file in, and {@link countSubstrings} then counts on every core. The output
 * is the same, byte for byte.
 *
 * @example
 * ```ts
 * import { Counter } from "xisto:collections";
 * const words = new Counter<string>();
 * for (const w of "the cat and the hat".split(" ")) words.add(w);
 * console.log(words.get("the"), words.get("dog")); // 2 0
 * console.log(words.mostCommon()[0]); // [ 'the', 2 ]
 * ```
 *
 * @packageDocumentation
 * @module xisto:collections
 */

import { availableParallelism } from "xisto:os";
import { parallelFrom } from "xisto:parallel";

/**
 * How many times each value occurs: a `Map` from the value to its count, which starts at 0.
 *
 * The values are kept in the order they were first added; {@link Counter.entries} and
 * {@link Counter.mostCommon} give them in that order (`mostCommon` among equal counts).
 *
 * @typeParam K - The type of the values counted: strings, numbers, anything a `Map` takes as a key.
 *
 * @remarks
 * Compatibility: the same class in Node and in xisto.
 *
 * @example
 * ```ts
 * import { Counter } from "xisto:collections";
 * const votes = new Counter<string>();
 * votes.add("yes");
 * votes.add("no");
 * votes.add("yes", 2);
 * console.log(votes.get("yes"), votes.size); // 3 2
 * ```
 */
export class Counter<K> {
  private readonly counts: Map<K, int> = new Map<K, int>();

  /**
   * Counts a value once more, or `count` more times.
   *
   * @param key - The value.
   * @param count - How many times to count it. Default: 1. It may be negative (the count is then lowered, and
   *   it stays in the counter even at 0 or below).
   *
   * @example
   * ```ts
   * const c = new Counter<string>();
   * c.add("a");
   * c.add("a", 4);
   * console.log(c.get("a")); // 5
   * ```
   */
  add(key: K, count: int = 1): void {
    if (this.counts.has(key)) this.counts.set(key, this.counts.get(key)! + count);
    else this.counts.set(key, count);
  }

  /**
   * How many times a value was counted.
   *
   * @param key - The value.
   * @returns Its count; 0 when it was never added.
   *
   * @example
   * ```ts
   * const c = new Counter<string>();
   * c.add("x");
   * console.log(c.get("x"), c.get("y")); // 1 0
   * ```
   */
  get(key: K): int {
    return this.counts.has(key) ? this.counts.get(key)! : 0;
  }

  /**
   * Whether a value was ever added.
   *
   * @param key - The value.
   * @returns `true` when it was added, whatever its count.
   *
   * @example
   * ```ts
   * const c = new Counter<int>();
   * c.add(7);
   * console.log(c.has(7), c.has(8)); // true false
   * ```
   */
  has(key: K): boolean {
    return this.counts.has(key);
  }

  /**
   * How many different values were added.
   *
   * @example
   * ```ts
   * const c = new Counter<string>();
   * c.add("a");
   * c.add("a");
   * c.add("b");
   * console.log(c.size); // 2
   * ```
   */
  get size(): int {
    return this.counts.size;
  }

  /**
   * Every value with its count, in the order the values were first added.
   *
   * @returns A new array of `[value, count]` pairs.
   *
   * @example
   * ```ts
   * const c = new Counter<string>();
   * c.add("b");
   * c.add("a");
   * c.add("b");
   * console.log(c.entries()); // [ [ 'b', 2 ], [ 'a', 1 ] ]
   * ```
   */
  entries(): [K, int][] {
    return Array.from(this.counts);
  }

  /**
   * Every value with its count, the most counted first; equal counts keep the order the values were first
   * added (the sort is stable).
   *
   * @returns A new array of `[value, count]` pairs.
   *
   * @example
   * ```ts
   * const c = new Counter<string>();
   * for (const ch of "banana") c.add(ch);
   * console.log(c.mostCommon()); // [ [ 'a', 3 ], [ 'n', 2 ], [ 'b', 1 ] ]
   * ```
   */
  mostCommon(): [K, int][] {
    return Array.from(this.counts).sort((a: [K, int], b: [K, int]) => b[1] - a[1]);
  }
}

// 2 ** n for n = 0..52 (a table, so the compiler knows every divisor taken from it is a power of two)
const POW2: int[] = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072,
  262144, 524288, 1048576, 2097152, 4194304, 8388608, 16777216, 33554432, 67108864, 134217728, 268435456,
  536870912, 1073741824, 2147483648, 4294967296, 8589934592, 17179869184, 34359738368, 68719476736,
  137438953472, 274877906944, 549755813888, 1099511627776, 2199023255552, 4398046511104, 8796093022208,
  17592186044416, 35184372088832, 70368744177664, 140737488355328, 281474976710656, 562949953421312,
  1125899906842624, 2251799813685248, 4503599627370496];

// 2 ** n for n = 0..16, and for n = 0..7 (the base of a character code): from these tables the compiler knows the
// range of a key made of them, so the arithmetic on it needs no overflow check
const POW2_16: int[] = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536];
const BASE: int[] = [1, 2, 4, 8, 16, 32, 64, 128];

const MIN_PIECE: int = 1 << 16; // fewer characters than this per thread are not worth a thread
const MAP_PIECES: int = 3; // pieces of a length counted in a Map (each piece's table is merged afterwards)

// which bytes occur in bytes[lo, hi): 1 at the byte's index; any byte from 128 up sets index 128
function bytesIn(bytes: Uint8Array, lo: int, hi: int): int[] {
  const seen: int[] = new Array<int>(129).fill(0);
  for (let i = lo; i < hi; i++) {
    const b = bytes[i];
    if (b >= 128) seen[128] = 1;
    else seen[b] = 1;
  }
  return seen;
}

// the substrings of `length` characters that start at lo..hi-1, each packed into a number (`bits` per
// character, the first one highest), in the order they first appear: key, count, first start, key, ...
// (letters: the byte of each character code)
function countPiece(bytes: Uint8Array, letters: int[], length: int, bits: int, lo: int, hi: int): int[] {
  const out: int[] = [];
  if (hi <= lo) return out;
  const code = new Uint8Array(256);
  for (let i = 0; i < letters.length; i++) code[letters[i]] = i;
  const base = BASE[bits];
  const end = hi + length - 1;
  if (bits * length <= 16) { // few possible keys: a table indexed by the key
    const mask = POW2_16[bits * length] - 1; // (keys of at most 16 bits: the shifts of JavaScript's 32 bits hold them)
    let key: int = 0;
    for (let i = lo; i < lo + length - 1; i++) key = ((key << bits) | code[bytes[i]]) & mask;
    const tally: int[] = new Array<int>(65536).fill(0);
    const start = key;
    for (let j = lo + length - 1; j < end; j++) {
      key = ((key << bits) | code[bytes[j]]) & mask;
      tally[key]++;
    }
    // the order they first appear: a second pass from the start, until every key counted has been seen (the few
    // keys of a short length all show up early)
    let distinct = 0;
    for (let k = 0; k <= mask; k++) if (tally[k] > 0) distinct++;
    const seen: int[] = new Array<int>(65536).fill(0);
    key = start;
    for (let j = lo + length - 1; j < end && out.length < 3 * distinct; j++) {
      key = ((key << bits) | code[bytes[j]]) & mask;
      if (seen[key] === 0) {
        seen[key] = 1;
        out.push(key);
        out.push(tally[key]);
        out.push(j + 1 - length);
      }
    }
    return out;
  }
  const top = POW2[bits * (length - 1)]; // what a key keeps of the substring before it (never past 52 bits)
  let key: int = 0;
  for (let i = lo; i < lo + length - 1; i++) key = key * base + code[bytes[i]];
  const counts = new Map<int, int>();
  const first: int[] = []; // where each key first starts, in the order of the map
  for (let j = lo + length - 1; j < end; j++) {
    key = (key % top) * base + code[bytes[j]];
    if (counts.has(key)) counts.set(key, counts.get(key)! + 1);
    else counts.set(key, 1);
    if (counts.size > first.length) first.push(j + 1 - length);
  }
  let i = 0;
  for (const [k, c] of counts) {
    out.push(k);
    out.push(c);
    out.push(first[i]);
    i++;
  }
  return out;
}

// the counter of one length: the pieces' triples in flat[from, to) added up in their order; each substring
// taken from the text where it first occurs
function counterOf(bytes: Uint8Array, flat: int[], from: int, to: int, length: int): Counter<string> {
  const slot = new Map<int, int>();
  const counts: int[] = [];
  const first: int[] = [];
  for (let i = from; i < to; i += 3) {
    const k = flat[i];
    if (slot.has(k)) counts[slot.get(k)!] += flat[i + 1];
    else {
      slot.set(k, counts.length);
      counts.push(flat[i + 1]);
      first.push(flat[i + 2]);
    }
  }
  const result = new Counter<string>();
  if (counts.length === 0) return result;
  const decoder = new TextDecoder();
  const piece = new Uint8Array(length);
  for (let i = 0; i < counts.length; i++) {
    for (let c = 0; c < length; c++) piece[c] = bytes[first[i] + c];
    result.add(decoder.decode(piece), counts[i]);
  }
  return result;
}

/**
 * Counts every substring of each given length in a text: for each length, how many times each run of that
 * many consecutive characters occurs (overlapping runs count, so `"aaa"` has `"aa"` twice).
 *
 * @param text - The text to count in.
 * @param lengths - The lengths of the substrings, in characters; each gets its own counter. A length of 0 or
 *   less, or longer than the text, gets an empty counter.
 * @returns One {@link Counter} per length, in the order of `lengths`. In each one the substrings are in the order
 *   they first occur in the text, so {@link Counter.mostCommon} breaks ties the way a counting loop would.
 *
 * @remarks
 * Compatibility: the same code in Node and in xisto, and the same counters. A text of ASCII characters is
 * counted with each substring packed into a number of a few bits per character (2 bits for a text of four
 * letters, such as DNA), so no substring is made until the counts are done; a text with other characters, or a
 * length whose packed substrings would not fit in 52 bits, is counted with strings. In Node it all runs on one
 * thread; in xisto the pieces of the text, for all the lengths together, are counted on every core at once.
 *
 * @throws Error when a count passes 2^53 − 1 (a text of more than 9 × 10^15 characters).
 *
 * @example
 * ```ts
 * import { countSubstrings } from "xisto:collections";
 * const [pairs, triples] = countSubstrings("GATTACA", [2, 3]);
 * console.log(pairs.get("TA"), pairs.size); // 1 5
 * console.log(triples.mostCommon()[0]); // [ 'GAT', 1 ]
 * ```
 */
export function countSubstrings(text: string, lengths: int[]): Counter<string>[] {
  const bytes = new TextEncoder().encode(text);
  const n = bytes.length;
  const threads = availableParallelism();
  // the characters that occur (each numbered by its place in letters: any numbering gives the same counts)
  const scans = Math.max(1, Math.min(threads, Math.trunc(n / MIN_PIECE)));
  const seen = parallelFrom(scans, (p) => bytesIn(bytes, Math.trunc(n * p / scans), Math.trunc(n * (p + 1) / scans)));
  const letters: int[] = [];
  let ascii = true;
  for (const s of seen) if (s[128] === 1) ascii = false;
  for (let b = 0; b < 128; b++) {
    let occurs = false;
    for (const s of seen) if (s[b] === 1) occurs = true;
    if (occurs) letters.push(b);
  }
  let bits: int = 1;
  while (POW2[bits] < letters.length) bits++;
  // the pieces to count: (length index, first start, end of the starts), those counted in a Map first, as they
  // take the longest
  const taskOf: int[] = [];
  const taskLo: int[] = [];
  const taskHi: int[] = [];
  for (let pass = 0; pass < 2; pass++) {
    for (let li = 0; li < lengths.length; li++) {
      const length = lengths[li];
      if (!ascii || length <= 0 || length > n || bits * length > 52) continue;
      const inMap = bits * length > 16;
      if (inMap !== (pass === 0)) continue;
      const size = n + 1 - length;
      const most = inMap ? MAP_PIECES : threads;
      const pieces = Math.max(1, Math.min(most, Math.trunc(size / MIN_PIECE)));
      for (let p = 0; p < pieces; p++) {
        taskOf.push(li);
        taskLo.push(Math.trunc(size * p / pieces));
        taskHi.push(Math.trunc(size * (p + 1) / pieces));
      }
    }
  }
  const parts = parallelFrom(taskOf.length, (t) => countPiece(bytes, letters, lengths[taskOf[t]], bits, taskLo[t], taskHi[t]));
  // each length's triples together, its pieces in text order (so first occurrences stay in order)
  let total = 0;
  for (const part of parts) total += part.length;
  const flat: int[] = new Array<int>(total).fill(0);
  const from: int[] = new Array<int>(lengths.length).fill(0);
  const to: int[] = new Array<int>(lengths.length).fill(0);
  let at = 0;
  for (let li = 0; li < lengths.length; li++) {
    from[li] = at;
    for (let t = 0; t < taskOf.length; t++) {
      if (taskOf[t] !== li) continue;
      const part = parts[t];
      for (let i = 0; i < part.length; i++) flat[at + i] = part[i];
      at += part.length;
    }
    to[li] = at;
  }
  const counters = parallelFrom(lengths.length, (li) => counterOf(bytes, flat, from[li], to[li], lengths[li]));
  // the lengths not packed: counted with strings
  for (let li = 0; li < lengths.length; li++) {
    const length = lengths[li];
    if (length <= 0 || length > n || (ascii && bits * length <= 52)) continue;
    const c = counters[li];
    for (let i = 0; i + length <= text.length; i++) c.add(text.substring(i, i + length));
  }
  return counters;
}
