/**
 * Byte arrays, searched and changed in place: reverse a range, map every byte through a table, find a character.
 *
 * The operations a program over raw bytes (a DNA file, a binary format, a text read as bytes) writes as loops,
 * with names it already knows from JavaScript (`reverse`, `map`, `indexOf`) and two conventions in every
 * function: characters are given as strings of one ASCII character (`"\n"`, not `10`), and options go in an
 * object whose fields can be left out (`{ from, to }`, `from` included and `to` excluded, as in `slice`).
 *
 * @remarks
 * Compatibility: this file is the module itself. In Node it runs as the reference implementation (plain loops),
 * with `node --import ./packages/xisto/register.mjs program.ts`; the xisto compiler builds the same file in and
 * turns its inner loops into vector code where the CPU has it (`memchr`, NEON/SSSE3 byte shuffles). Both give
 * the same output, byte for byte; xisto is the faster one.
 *
 * @example
 * ```ts
 * import { byteMap, indexOf, reverse } from "xisto:bytes";
 * const dna = new TextEncoder().encode(">seq\nACG\nTT\n");
 * const body = indexOf(dna, "\n") + 1;
 * reverse(dna, { map: byteMap("ACGT", "TGCA"), skip: "\n", from: body });
 * console.log(new TextDecoder().decode(dna)); // ">seq\nAAC\nGT\n"
 * ```
 *
 * @packageDocumentation
 * @module xisto:bytes
 */

/** The part of a byte array an operation works on: `from` included, `to` excluded, as in `slice`. */
export interface Range {
  /** The first index. Default: 0. */
  from?: int;
  /** The index after the last one. Default: the length of the array. */
  to?: int;
}

/** Options of {@link reverse}. */
export interface ReverseOptions {
  /** A map of 256 bytes (made with {@link byteMap}): every byte is also mapped through it, in the same pass. */
  map?: Uint8Array;
  /** One ASCII character whose bytes stay where they are; the other bytes are reversed around them. */
  skip?: string;
  /** The first index. Default: 0. */
  from?: int;
  /** The index after the last one. Default: the length of the array. */
  to?: int;
}

/** Options of {@link indexOf} and {@link lastIndexOf}. */
export interface SearchOptions {
  /** Where the search starts; a negative value counts from the end. Default: the first byte (indexOf), the last one (lastIndexOf). */
  from?: int;
}

/** Options of {@link byteMap}. */
export interface ByteMapOptions {
  /** Also map the other case of each letter of `from` (upper and lower). Default: false. */
  ignoreCase?: boolean;
}

const NO_MAP = new Uint8Array(0);

/**
 * Makes a map of the 256 byte values, for {@link mapBytes} and {@link reverse}.
 *
 * Every byte goes to itself, except the k-th character of `from`, which goes to the k-th character of `to`.
 * The map is a plain `Uint8Array` indexed by byte value, so `map[b]` is what byte `b` becomes.
 *
 * @param from - The characters to change, in ASCII.
 * @param to - What each character of `from` becomes, in ASCII and in the same order.
 * @param opts - Options.
 * @param opts.ignoreCase - Also map the other case of each letter of `from`. Default: false.
 * @returns A new array of 256 bytes.
 * @throws Error when `from` and `to` have different lengths, or when one of their characters is not ASCII.
 *
 * @remarks
 * Compatibility: the same in Node and in xisto (a loop over 256 bytes, made once).
 *
 * @example
 * ```ts
 * import { byteMap } from "xisto:bytes";
 * const complement = byteMap("ACGT", "TGCA", { ignoreCase: true });
 * console.log(String.fromCharCode(complement[97])); // "T": 'a' is mapped too
 * ```
 */
export function byteMap(from: string, to: string, opts: ByteMapOptions = {}): Uint8Array {
  const { ignoreCase = false } = opts;
  if (from.length !== to.length) throw new Error("byteMap: from and to must have the same length");
  const t = new Uint8Array(256);
  for (let i = 0; i < 256; i++) t[i] = i;
  for (let k = 0; k < from.length; k++) {
    const f = asciiAt(from, k, "byteMap"), v = asciiAt(to, k, "byteMap");
    t[f] = v;
    if (ignoreCase) {
      t[f | 0x20] = v;
      t[f & ~0x20] = v;
    }
  }
  return t;
}

/**
 * Replaces every byte of a range by what a map gives for it, in place.
 *
 * `Array.prototype.map`, byte by byte and without a new array: `buf[i] = map[buf[i]]` for every `i` of the range.
 *
 * @param buf - The bytes to change.
 * @param map - A map of 256 bytes, made with {@link byteMap}.
 * @param opts - Options.
 * @param opts.from - The first index. Default: 0.
 * @param opts.to - The index after the last one. Default: `buf.length`.
 * @throws Error when `map` does not have 256 bytes, or when the range is not inside `buf`.
 *
 * @remarks
 * Compatibility: in Node, a loop; in xisto, the same loop of table loads, compiled.
 *
 * @example
 * ```ts
 * import { byteMap, mapBytes } from "xisto:bytes";
 * const text = new TextEncoder().encode("gattaca");
 * mapBytes(text, byteMap("acgt", "ACGT"), { to: 4 });
 * console.log(new TextDecoder().decode(text)); // "GATTaca"
 * ```
 */
export function mapBytes(buf: Uint8Array, map: Uint8Array, opts: Range = {}): void {
  const { from = 0, to = buf.length } = opts;
  checkMap(map, "mapBytes");
  checkRange(buf, from, to, "mapBytes");
  for (let i = from; i < to; i++) buf[i] = map[buf[i]];
}

/**
 * Reverses the bytes of a range, in place, optionally mapping them and leaving one character where it is.
 *
 * With `map`, each byte is also mapped through it, in the same pass. With `skip`, the bytes of that character
 * stay where they are and the others are reversed around them: text written in lines keeps its line breaks.
 * Both together give the reverse complement of a DNA sequence written in lines.
 *
 * @param buf - The bytes to change.
 * @param opts - Options.
 * @param opts.map - A map of 256 bytes ({@link byteMap}) to map every byte through. Default: none.
 * @param opts.skip - One ASCII character whose bytes stay in place. Default: none.
 * @param opts.from - The first index. Default: 0.
 * @param opts.to - The index after the last one. Default: `buf.length`.
 * @throws Error when `map` does not have 256 bytes, when `skip` is not one ASCII character, or when the range
 * is not inside `buf`.
 *
 * @remarks
 * Compatibility: in Node, loops that swap from both ends. In xisto, a vector byte shuffle on arm64 (NEON
 * `tbl`) and on x86 built for a CPU with SSSE3 (`--cpu=native`: `pshufb`), table loads otherwise; with `skip`,
 * one such run between each two kept bytes, found with `memchr` and a 16-byte search back.
 *
 * @example
 * ```ts
 * import { byteMap, reverse } from "xisto:bytes";
 * const decode = (b: Uint8Array): string => new TextDecoder().decode(b);
 * const lines = new TextEncoder().encode("AC\nGT");
 * reverse(lines, { skip: "\n" });
 * console.log(decode(lines)); // "TG\nCA"
 * const dna = new TextEncoder().encode("AAC\nGT\n");
 * reverse(dna, { map: byteMap("ACGT", "TGCA"), skip: "\n" });
 * console.log(decode(dna)); // "ACG\nTT\n"
 * ```
 */
export function reverse(buf: Uint8Array, opts: ReverseOptions = {}): void {
  const { map = NO_MAP, skip = "", from = 0, to = buf.length } = opts;
  if (map !== NO_MAP) checkMap(map, "reverse");
  checkRange(buf, from, to, "reverse");
  if (skip !== "") reverseAround(buf, map === NO_MAP ? byteMap("", "") : map, byteOf(skip, "reverse"), from, to);
  else if (map !== NO_MAP) reverseMapped(buf, map, from, to);
  else reversePlain(buf, from, to);
}

/**
 * Finds the first byte of a character.
 *
 * `Uint8Array.prototype.indexOf`, given the character instead of its byte value, so the call reads as what it
 * looks for: `indexOf(input, "\n")`.
 *
 * @param buf - The bytes to search.
 * @param char - One ASCII character.
 * @param opts - Options.
 * @param opts.from - Where to start; a negative value counts from the end. Default: 0.
 * @returns The index of the first byte equal to `char` at `from` or after it, or -1 when there is none.
 * @throws Error when `char` is not one ASCII character.
 *
 * @remarks
 * Compatibility: in Node, `Uint8Array.prototype.indexOf`; in xisto, `memchr`.
 *
 * @example
 * ```ts
 * import { indexOf } from "xisto:bytes";
 * const lines = new TextEncoder().encode("one\ntwo\n");
 * console.log(indexOf(lines, "\n"), indexOf(lines, "\n", { from: 4 }), indexOf(lines, ">")); // 3 7 -1
 * ```
 */
export function indexOf(buf: Uint8Array, char: string, opts: SearchOptions = {}): int {
  const { from = 0 } = opts;
  return buf.indexOf(byteOf(char, "indexOf"), from);
}

/**
 * Finds the last byte of a character.
 *
 * `Uint8Array.prototype.lastIndexOf`, given the character instead of its byte value: the search goes back from
 * `from`.
 *
 * @param buf - The bytes to search.
 * @param char - One ASCII character.
 * @param opts - Options.
 * @param opts.from - Where to start, going back; a negative value counts from the end. Default: the last byte.
 * @returns The index of the last byte equal to `char` at `from` or before it, or -1 when there is none.
 * @throws Error when `char` is not one ASCII character.
 *
 * @remarks
 * Compatibility: in Node, `Uint8Array.prototype.lastIndexOf`; in xisto, a search of 8 bytes at a time.
 *
 * @example
 * ```ts
 * import { lastIndexOf } from "xisto:bytes";
 * const lines = new TextEncoder().encode("one\ntwo\n");
 * console.log(lastIndexOf(lines, "\n"), lastIndexOf(lines, "\n", { from: 6 })); // 7 3
 * ```
 */
export function lastIndexOf(buf: Uint8Array, char: string, opts: SearchOptions = {}): int {
  const { from = buf.length - 1 } = opts;
  return buf.lastIndexOf(byteOf(char, "lastIndexOf"), from);
}

// ---- inside (not exported)

function byteOf(char: string, fn: string): int {
  if (char.length !== 1) throw new Error(`${fn}: expected one ASCII character, got ${JSON.stringify(char)}`);
  return asciiAt(char, 0, fn);
}

function asciiAt(s: string, k: int, fn: string): int {
  const c = Math.trunc(s.charCodeAt(k)); // (a char code is an int)
  if (c > 127) throw new Error(`${fn}: expected one ASCII character, got ${JSON.stringify(s.slice(k, k + 1))}`);
  return c;
}

function checkMap(map: Uint8Array, fn: string): void {
  if (map.length !== 256) throw new Error(`${fn}: the map must have 256 bytes (make it with byteMap)`);
}

// (Node would write past the end of a Uint8Array without a word, and xisto stops: both stop here instead.)
function checkRange(buf: Uint8Array, from: int, to: int, fn: string): void {
  if (from < 0 || to > buf.length || from > to) throw new Error(`${fn}: the range ${from}..${to} is not inside the ${buf.length} bytes`);
}

function reversePlain(a: Uint8Array, start: int, end: int): void {
  const s = start + end - 1; // a[i] trades places with a[s - i]
  for (let i = start; i < (s + 1) >> 1; i++) {
    const t = a[i];
    a[i] = a[s - i];
    a[s - i] = t;
  }
}

function reverseMapped(a: Uint8Array, table: Uint8Array, start: int, end: int): void {
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

// Two cursors, one from each end, each inside a stretch between two `keep` bytes; a run in which neither leaves
// its stretch is one trade loop.
function reverseAround(a: Uint8Array, table: Uint8Array, keep: int, start: int, end: int): void {
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
