/**
 * Random sequences you can repeat: a linear congruential generator with the seed and constants you choose, and
 * text whose characters are drawn from it with the probabilities you give.
 *
 * `Math.random()` cannot be seeded, so its sequence changes on every run. A generator from here gives the same
 * sequence for the same seed, in Node and in xisto: for tests, simulations, and generated data that must come out
 * the same every time.
 *
 * @remarks
 * Compatibility: this file is the module itself. In Node it runs as the reference implementation, with
 * `node --import ./packages/xisto/register.mjs program.ts`; the xisto compiler builds the same file in. The values
 * and the characters are the same, byte for byte. {@link WeightedCharacters.pick} is the fast way to draw many
 * characters: when the modulus is small it works the character for every value of the generator out once into a
 * table, and it runs four steps of the generator at a time (each value from the one four steps back), which gives
 * the very same sequence.
 *
 * @example
 * ```ts
 * import { LinearCongruential, WeightedCharacters } from "xisto:random";
 * const random = new LinearCongruential({ seed: 42, multiplier: 3877, increment: 29573, modulus: 139968 });
 * console.log(random.next(), random.next()); // 52439 102040
 * const dna = new WeightedCharacters(random, "acgt", [0.3, 0.2, 0.2, 0.3]);
 * console.log(dna.pick(10)); // ten characters, each drawn with those probabilities
 * ```
 *
 * @packageDocumentation
 * @module xisto:random
 */

/** The constants of a {@link LinearCongruential} generator: each value is (multiplier · previous + increment) mod modulus. */
export interface LinearCongruentialOptions {
  /** The value before the first one (0 ≤ seed < modulus). */
  seed: int;
  /** What each value is multiplied by (0 or more). */
  multiplier: int;
  /** What is added after the multiplication (0 or more). */
  increment: int;
  /** The values stay below it (above 0). */
  modulus: int;
}

/**
 * A linear congruential generator: each value is (multiplier · previous + increment) mod modulus, from the seed.
 * The classic repeatable pseudo-random sequence (the one in the "fasta" program, in old C libraries, in
 * Park–Miller with an increment of 0).
 *
 * @remarks
 * Compatibility: the same values in Node and in xisto. The arithmetic is exact integer arithmetic, so the
 * constants must keep multiplier · (modulus − 1) + increment below 2^53.
 *
 * @example
 * ```ts
 * const dice = new LinearCongruential({ seed: 1, multiplier: 48271, increment: 0, modulus: 2147483647 });
 * const roll = 1 + dice.next() % 6;
 * ```
 */
export class LinearCongruential {
  /** The values stay below this. */
  readonly modulus: int;
  /** What each value is multiplied by. */
  readonly multiplier: int;
  /** What is added after the multiplication. */
  readonly increment: int;
  /** The last value given (the seed before the first call to {@link LinearCongruential.next}). */
  last: int;

  /**
   * @param options - `seed`, `multiplier`, `increment` and `modulus`.
   * @throws Error when the modulus is not above 0, the seed is not in [0, modulus), the multiplier or the
   *   increment is negative, or multiplier · (modulus − 1) + increment reaches 2^53.
   */
  constructor(options: LinearCongruentialOptions) {
    if (options.modulus <= 0) throw new Error("modulus must be above 0");
    if (options.seed < 0 || options.seed >= options.modulus) throw new Error("seed must be 0 or more and below the modulus");
    if (options.multiplier < 0 || options.increment < 0) throw new Error("multiplier and increment must be 0 or more");
    if (Number(options.multiplier) * Number(options.modulus - 1) + Number(options.increment) >= 9007199254740992) { // (in doubles: the product itself may be too big for an int)
      throw new Error("multiplier * (modulus - 1) + increment must stay below 2^53");
    }
    this.modulus = options.modulus;
    this.multiplier = options.multiplier;
    this.increment = options.increment;
    this.last = options.seed;
  }

  /**
   * The next value of the sequence.
   *
   * @returns A whole number, 0 or more and below the modulus.
   *
   * @example
   * ```ts
   * const r = new LinearCongruential({ seed: 42, multiplier: 3877, increment: 29573, modulus: 139968 });
   * r.next(); // 52439
   * ```
   */
  next(): int {
    this.last = (this.last * this.multiplier + this.increment) % this.modulus;
    return this.last;
  }

  /** @internal The length of the cycle the sequence runs in; 0 until {@link LinearCongruential.findCycle}. */
  cycleLength: int = 0;
  /** @internal The value at place 0 of the cycle: the first value of it the sequence reached. */
  cycleStart: int = 0;
  /** @internal The place on the cycle of the value after `last`, while `last` is still `placedLast`. */
  nextPlace: int = 0;
  /** @internal The `last` that {@link LinearCongruential.nextPlace} was worked out for (−1: none). */
  placedLast: int = -1;
  /** @internal Where each value sits on the cycle, −1 off it: made only when `last` moved where `nextPlace` does not follow. */
  cyclePlaces: int[] = [];

  /**
   * @internal The cycle of the values after `last`: a generator with a small modulus repeats itself, and once its
   * values are known in order, many of them are a copy. Found once (the modulus at most 2^21), kept as its start
   * and length only (a value at a place is worked out with {@link LinearCongruential.valueAt}): the table of
   * places is made only if the sequence is moved somewhere the place cannot be followed.
   * @returns Whether the next value is on the cycle found ({@link LinearCongruential.nextPlace} is then its place).
   */
  findCycle(): boolean {
    const m = this.modulus;
    if (m > 2097152) return false;
    const a: int = this.multiplier % m, c: int = this.increment % m;
    const next: int = (this.last * this.multiplier + this.increment) % m;
    if (this.cycleLength === 0) {
      // the values from the next one on, marked until one comes back: that one starts the cycle (the values before
      // it lead into the cycle and never come back)
      const seen = new Uint8Array((m + 7) >> 3);
      let v: int = next;
      let k: int = 0;
      while ((seen[v >> 3] & (1 << (v & 7))) === 0) {
        seen[v >> 3] |= 1 << (v & 7);
        v = (v * a + c) % m;
        k++;
      }
      let len: int = k;
      if (v !== next) { // a lead-in: the cycle is shorter than the walk
        len = 1;
        for (let w: int = (v * a + c) % m; w !== v; w = (w * a + c) % m) len++;
      }
      this.cycleStart = v;
      this.cycleLength = len;
      if (v === next) {
        this.placedLast = this.last;
        this.nextPlace = 0;
      }
    }
    if (this.last === this.placedLast) return true;
    if (this.cyclePlaces.length === 0) {
      const places: int[] = new Array<int>(m).fill(-1);
      let v: int = this.cycleStart;
      for (let k = 0; k < this.cycleLength; k++) {
        places[v] = k;
        v = (v * a + c) % m;
      }
      this.cyclePlaces = places;
    }
    const at: int = this.cyclePlaces[next];
    if (at < 0) return false;
    this.placedLast = this.last;
    this.nextPlace = at;
    return true;
  }

  /** @internal The value at place `place` of the cycle found by {@link LinearCongruential.findCycle}: the start moved on that many steps at once (the steps composed by squaring). */
  valueAt(place: int): int {
    const m = this.modulus;
    let mulAll: int = 1, addAll: int = 0; // the steps taken so far: x -> mulAll * x + addAll
    let mul: int = this.multiplier % m, add: int = this.increment % m; // 2^j steps at once
    for (let k = place; k > 0; k = k >> 1) {
      if ((k & 1) === 1) {
        mulAll = (mulAll * mul) % m;
        addAll = (addAll * mul + add) % m;
      }
      add = (add * mul + add) % m;
      mul = (mul * mul) % m;
    }
    return (this.cycleStart * mulAll + addAll) % m;
  }
}

/** How {@link WeightedCharacters.pick} lays out the characters. */
export interface PickOptions {
  /** A newline after every `lineLength` characters and after the last one; 0 or not given: no newlines. */
  lineLength?: int;
}

/**
 * Text drawn at random, one character at a time, each character with its own probability, from a
 * {@link LinearCongruential} generator: for each character the generator gives a value v, and the character is
 * the first one whose running total of probabilities is above v / modulus (DNA, test data, Markov text).
 *
 * @remarks
 * Compatibility: the same characters in Node and in xisto. The comparison is the double v / modulus against the
 * running total in doubles, the same arithmetic as the plain loop
 * `for (const [c, p] of cumulative) if (v / m < p) return c`.
 *
 * @example
 * ```ts
 * const random = new LinearCongruential({ seed: 42, multiplier: 3877, increment: 29573, modulus: 139968 });
 * const coin = new WeightedCharacters(random, "HT", [0.5, 0.5]);
 * console.log(coin.pick(20));
 * ```
 */
export class WeightedCharacters {
  private readonly random: LinearCongruential;
  private readonly codes: Uint8Array;
  private readonly totals: number[] = [];
  private cycleText: Uint8Array = new Uint8Array(0); // the characters of the generator's cycle, twice over, made on first use

  /**
   * @param random - The generator the characters are drawn from (it moves on one value per character).
   * @param characters - The characters to draw from, one per probability (ASCII).
   * @param probabilities - The probability of each character; they must add up to 1 (within 10^-9).
   * @throws Error when the lengths differ, a character is not ASCII, a probability is negative, or they do not
   *   add up to 1.
   */
  constructor(random: LinearCongruential, characters: string, probabilities: number[]) {
    if (characters.length !== probabilities.length || characters.length === 0) {
      throw new Error("give one probability for each character");
    }
    this.random = random;
    this.codes = new Uint8Array(characters.length);
    let sum = 0;
    for (let k = 0; k < characters.length; k++) {
      const code: int = Math.trunc(characters.charCodeAt(k));
      if (code > 127) throw new Error("the characters must be ASCII");
      if (probabilities[k] < 0) throw new Error("a probability cannot be negative");
      this.codes[k] = code;
      sum += probabilities[k];
      this.totals.push(sum);
    }
    if (Math.abs(sum - 1) > 1e-9) throw new Error("the probabilities must add up to 1");
  }

  /**
   * Draws `count` characters.
   *
   * @param count - How many characters (0 or more).
   * @param options - `lineLength`: when above 0, a newline after every `lineLength` characters and after the
   *   last one (the text as lines of that length, the last one shorter), not counted in `count`.
   * @returns The characters, in the order drawn.
   * @throws Error when `count` or `lineLength` is negative.
   *
   * @example
   * ```ts
   * dna.pick(5);                      // "agtta"
   * dna.pick(130, { lineLength: 60 }); // two lines of 60 and one of 10, each ending in "\n"
   * ```
   */
  pick(count: int, options: PickOptions = {}): string {
    if (count < 0) throw new Error("count must be 0 or more");
    const { lineLength = 0 } = options;
    const width: int = lineLength;
    if (width < 0) throw new Error("lineLength must be 0 or more");
    const lines: int = width > 0 ? Math.trunc((count + width - 1) / width) : 0;
    const out = new Uint8Array(count + lines);
    const r = this.random;
    const m = r.modulus;
    if (count > 0 && r.findCycle() && width < r.cycleLength) {
      this.copyFromCycle(out, count, width);
    } else {
      let at = 0, col = 0;
      for (let i = 0; i < count; i++) {
        out[at++] = this.codeOf(r.next() / m);
        if (width > 0 && ++col === width) { out[at++] = 10; col = 0; }
      }
      if (width > 0 && col > 0) out[at] = 10;
    }
    return new TextDecoder().decode(out);
  }

  // the character for a draw r = value / modulus: the first whose running total is above r (the last one past them all)
  private codeOf(r: number): int {
    const totals = this.totals;
    let k = 0;
    while (k < totals.length - 1 && !(r < totals[k])) k++;
    return this.codes[k];
  }

  // the characters drawn, copied from the generator's cycle: the character of each value on it worked out once,
  // and the start of the cycle written again after its end so that a line never wraps around it
  private copyFromCycle(out: Uint8Array, count: int, width: int): void {
    const r = this.random;
    const n = r.cycleLength;
    const step: int = width > 0 ? width : n; // (width below n: pick checks)
    if (this.cycleText.length < n + step) {
      const m = r.modulus;
      const a: int = r.multiplier % m, c: int = r.increment % m;
      const text = new Uint8Array(n + step);
      let v: int = r.cycleStart;
      for (let k = 0; k < n; k++) {
        text[k] = this.codeOf(v / m);
        v = (v * a + c) % m;
      }
      for (let k = 0; k < step; k++) text[n + k] = text[k];
      this.cycleText = text;
    }
    const text = this.cycleText;
    let p: int = r.nextPlace;
    let at: int = 0, left: int = count;
    while (left > 0) {
      const len = left < step ? left : step;
      for (let k = 0; k < len; k++) out[at + k] = text[p + k];
      at += len;
      if (width > 0 && (len === width || len === left)) { out[at] = 10; at++; }
      left -= len;
      p += len;
      if (p >= n) p -= n;
    }
    r.last = r.valueAt(p === 0 ? n - 1 : p - 1);
    r.placedLast = r.last;
    r.nextPlace = p;
  }
}

