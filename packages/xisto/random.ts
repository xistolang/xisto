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

  /** @internal The values of the cycle the sequence runs in, in order; empty until {@link LinearCongruential.findCycle}. */
  cycleValues: int[] = [];
  /** @internal Where each value sits in {@link LinearCongruential.cycleValues}; −1 off the cycle. */
  cyclePlaces: int[] = [];

  /**
   * @internal The cycle of the values after `last`: a generator with a small modulus repeats itself, and once its
   * values are known in order, many of them are a copy. Made once (the modulus at most 2^21).
   * @returns Whether the next value is on the cycle found.
   */
  findCycle(): boolean {
    const m = this.modulus;
    if (m > 2097152) return false;
    if (this.cycleValues.length === 0) {
      const places: int[] = new Array<int>(m).fill(-1);
      const order: int[] = new Array<int>(m).fill(0);
      let v: int = (this.last * this.multiplier + this.increment) % m;
      let k = 0;
      while (places[v] < 0) {
        places[v] = k;
        order[k] = v;
        k++;
        v = (v * this.multiplier + this.increment) % m;
      }
      const first = places[v]; // the values before it lead into the cycle and never come back
      for (let j = 0; j < first; j++) places[order[j]] = -1;
      for (let j = first; j < k; j++) places[order[j]] -= first;
      this.cycleValues = order.slice(first, k);
      this.cyclePlaces = places;
    }
    return this.cyclePlaces[(this.last * this.multiplier + this.increment) % m] >= 0;
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
    if (count > 0 && r.findCycle() && width < r.cycleValues.length) {
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
  // and the cycle written twice so that a line never wraps around its end
  private copyFromCycle(out: Uint8Array, count: int, width: int): void {
    const r = this.random;
    const values = r.cycleValues;
    const n = values.length;
    if (this.cycleText.length !== 2 * n) {
      const m = r.modulus;
      const text = new Uint8Array(2 * n);
      for (let k = 0; k < n; k++) {
        const code = this.codeOf(values[k] / m);
        text[k] = code;
        text[k + n] = code;
      }
      this.cycleText = text;
    }
    const text = this.cycleText;
    let p: int = r.cyclePlaces[(r.last * r.multiplier + r.increment) % r.modulus];
    const step: int = width > 0 ? width : n; // (width below n: pick checks)
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
    r.last = values[p === 0 ? n - 1 : p - 1];
  }
}

