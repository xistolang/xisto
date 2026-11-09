/**
 * The Mandelbrot set: whether a point belongs to it, and a whole row of a bitmap of it at once.
 *
 * A point c of the complex plane belongs to the set when z, starting at 0 and replaced by z² + c again and again,
 * never leaves the disc of radius 2. A program can only run a number of steps, so "belongs" here means: after
 * each of `iterations` steps, |z| is still at most `radius` (the usual escape-time test).
 *
 * @remarks
 * Compatibility: this file is the module itself. In Node it runs as the reference implementation, with
 * `node --import ./packages/xisto/register.mjs program.ts`; the xisto compiler builds the same file in. Both give
 * the same answers, bit for bit: every step is the same double arithmetic as the plain loop
 * (`zi = 2 * zr * zi + ci; zr = zr² − zi² + cr`), and |z|² is compared as `zr * zr + zi * zi`.
 * {@link mandelbrotRow} works eight points at a time and tests for escape every few steps instead of every
 * step, only where that cannot change an answer: for |c| < 2 and a radius of 2 or more, a point that has left
 * the disc never comes back (|z² + c| ≥ |z|² − |c| > |z|), so its answer is already known. Elsewhere it tests
 * every step, as the plain loop does.
 *
 * @example
 * ```ts
 * import { inMandelbrotSet } from "xisto:fractal";
 * console.log(inMandelbrotSet(-1, 0, { iterations: 50, radius: 2 })); // true
 * console.log(inMandelbrotSet(1, 1, { iterations: 50, radius: 2 })); // false
 * ```
 *
 * @packageDocumentation
 * @module xisto:fractal
 */

/** How long and how far to follow z ← z² + c. */
export interface EscapeOptions {
  /** How many steps of z ← z² + c to run (0 or more). */
  iterations: int;
  /** The radius z must stay within: |z| ≤ radius after every step (above 0). */
  radius: number;
}

function checkOptions(options: EscapeOptions): void {
  if (options.iterations < 0) throw new Error("iterations must be 0 or more");
  if (!(options.radius > 0)) throw new Error("radius must be above 0");
}

/**
 * Whether the point c = real + imaginary·i belongs to the Mandelbrot set: |z| stays at most `radius` after each
 * of `iterations` steps of z ← z² + c, from z = 0.
 *
 * @param real - The real part of c.
 * @param imaginary - The imaginary part of c.
 * @param options - `iterations` (how many steps) and `radius` (how far z may go).
 * @returns `true` when z never left the disc.
 * @throws Error when `iterations` is negative or `radius` is not above 0.
 *
 * @remarks
 * Compatibility: the same in Node and in xisto.
 *
 * @example
 * ```ts
 * inMandelbrotSet(0, 0, { iterations: 50, radius: 2 });   // true
 * inMandelbrotSet(0.5, 0.5, { iterations: 50, radius: 2 }); // false
 * ```
 */
export function inMandelbrotSet(real: number, imaginary: number, options: EscapeOptions): boolean {
  checkOptions(options);
  return stays(real, imaginary, options.iterations, options.radius * options.radius);
}

// the plain loop: z from 0, the test before every step
function stays(cr: number, ci: number, iterations: int, r2: number): boolean {
  let zr = 0.0, zi = 0.0, tr = 0.0, ti = 0.0;
  for (let i = 0; i < iterations && tr + ti <= r2; i++) {
    zi = 2 * zr * zi + ci;
    zr = tr - ti + cr;
    tr = zr * zr;
    ti = zi * zi;
  }
  return tr + ti <= r2;
}

/**
 * One row of a black-and-white bitmap of the Mandelbrot set: for each point c = reals[k] + imaginary·i, one bit,
 * 1 when the point belongs to the set ({@link inMandelbrotSet}). Eight points per byte, the first point in the
 * highest bit, and the last byte padded with 0 bits: the rows of a PBM (P4) image.
 *
 * @param reals - The real part of each point of the row, in order.
 * @param imaginary - The imaginary part shared by the whole row.
 * @param options - `iterations` (how many steps) and `radius` (how far z may go).
 * @returns `Math.ceil(reals.length / 8)` bytes.
 * @throws Error when `iterations` is negative or `radius` is not above 0.
 *
 * @remarks
 * Compatibility: the same bits in Node and in xisto. It is the fast way to draw the set: eight points at a time,
 * and a byte whose eight points have all escaped is finished at once (see the module's notes on why that is exact).
 *
 * @example
 * ```ts
 * import { mandelbrotRow } from "xisto:fractal";
 * const size = 200;
 * const reals = new Float64Array(size);
 * for (let x = 0; x < size; x++) reals[x] = 2 * x / size - 1.5;
 * process.stdout.write(`P4\n${size} ${size}\n`);
 * for (let y = 0; y < size; y++) {
 *   process.stdout.write(mandelbrotRow(reals, 2 * y / size - 1, { iterations: 50, radius: 2 }));
 * }
 * ```
 */
export function mandelbrotRow(reals: Float64Array, imaginary: number, options: EscapeOptions): Uint8Array {
  checkOptions(options);
  const n = reals.length;
  const bits = new Uint8Array((n + 7) >> 3);
  const iterations = options.iterations, r2 = options.radius * options.radius;
  const ci = imaginary;
  // eight points at a time where an escaped point provably stays out: |c|² below 3.99 (|c| < 2 with room for
  // rounding) and a radius of 2 or more; and at least one step, which takes z from 0 to c exactly
  const fast = options.radius >= 2 && iterations >= 1 && ci * ci < 3.99;
  let prev = 0; // the byte before: 0 when its eight points all escaped, so these are worth testing early
  let x = 0;
  for (; x + 8 <= n; x += 8) {
    const r0 = reals[x], r1 = reals[x + 1], r2c = reals[x + 2], r3 = reals[x + 3];
    const r4 = reals[x + 4], r5 = reals[x + 5], r6 = reals[x + 6], r7 = reals[x + 7];
    let b = 0;
    if (fast && near(r0, ci) && near(r1, ci) && near(r2c, ci) && near(r3, ci) && near(r4, ci) && near(r5, ci) && near(r6, ci) && near(r7, ci)) {
      b = byte8(r0, r1, r2c, r3, r4, r5, r6, r7, ci, iterations, r2, prev);
    } else {
      if (stays(r0, ci, iterations, r2)) b |= 128;
      if (stays(r1, ci, iterations, r2)) b |= 64;
      if (stays(r2c, ci, iterations, r2)) b |= 32;
      if (stays(r3, ci, iterations, r2)) b |= 16;
      if (stays(r4, ci, iterations, r2)) b |= 8;
      if (stays(r5, ci, iterations, r2)) b |= 4;
      if (stays(r6, ci, iterations, r2)) b |= 2;
      if (stays(r7, ci, iterations, r2)) b |= 1;
    }
    bits[x >> 3] = b;
    prev = b;
  }
  if (x < n) { // the last 1 to 7 points, one at a time, into the high bits of the last byte
    let b = 0;
    for (let k = 0; x + k < n; k++) if (stays(reals[x + k], ci, iterations, r2)) b |= 128 >> k;
    bits[x >> 3] = b;
  }
  return bits;
}

function near(cr: number, ci: number): boolean {
  return cr * cr + ci * ci < 3.99;
}

// eight points, |c| < 2 and radius ≥ 2 checked: the first step gives z = c exactly, then the steps with
// the escape tested every fourth one (only when the byte before was all out: inside the set nothing escapes, and the
// test would only cost); all eight out at a test: they stay out, the byte is 0
function byte8(r0: number, r1: number, r2: number, r3: number, r4: number, r5: number, r6: number, r7: number,
               ci: number, iterations: int, lim: number, prev: int): int {
  let zr0 = r0, zr1 = r1, zr2 = r2, zr3 = r3, zr4 = r4, zr5 = r5, zr6 = r6, zr7 = r7;
  let zi0 = ci, zi1 = ci, zi2 = ci, zi3 = ci, zi4 = ci, zi5 = ci, zi6 = ci, zi7 = ci;
  let tr0 = r0 * r0, tr1 = r1 * r1, tr2 = r2 * r2, tr3 = r3 * r3, tr4 = r4 * r4, tr5 = r5 * r5, tr6 = r6 * r6, tr7 = r7 * r7;
  const cc = ci * ci;
  let ti0 = cc, ti1 = cc, ti2 = cc, ti3 = cc, ti4 = cc, ti5 = cc, ti6 = cc, ti7 = cc;
  // one loop with the test every fourth step, not blocks of four and a remainder loop: with a step count known
  // only at run time, the remainder loop made the C compiler pack the lanes differently across the two loops
  // (blends and spills; Xeon 5,69 -> 3,95 u), and the test lands on the same steps (5, 9, 13...)
  const steps = iterations - 1;
  for (let i = 1; i <= steps; i++) {
    zi0 = 2 * zr0 * zi0 + ci; zr0 = tr0 - ti0 + r0; tr0 = zr0 * zr0; ti0 = zi0 * zi0;
    zi1 = 2 * zr1 * zi1 + ci; zr1 = tr1 - ti1 + r1; tr1 = zr1 * zr1; ti1 = zi1 * zi1;
    zi2 = 2 * zr2 * zi2 + ci; zr2 = tr2 - ti2 + r2; tr2 = zr2 * zr2; ti2 = zi2 * zi2;
    zi3 = 2 * zr3 * zi3 + ci; zr3 = tr3 - ti3 + r3; tr3 = zr3 * zr3; ti3 = zi3 * zi3;
    zi4 = 2 * zr4 * zi4 + ci; zr4 = tr4 - ti4 + r4; tr4 = zr4 * zr4; ti4 = zi4 * zi4;
    zi5 = 2 * zr5 * zi5 + ci; zr5 = tr5 - ti5 + r5; tr5 = zr5 * zr5; ti5 = zi5 * zi5;
    zi6 = 2 * zr6 * zi6 + ci; zr6 = tr6 - ti6 + r6; tr6 = zr6 * zr6; ti6 = zi6 * zi6;
    zi7 = 2 * zr7 * zi7 + ci; zr7 = tr7 - ti7 + r7; tr7 = zr7 * zr7; ti7 = zi7 * zi7;
    if ((i & 3) === 0 && prev === 0) {
      const m0 = tr0 + ti0, m1 = tr1 + ti1, m2 = tr2 + ti2, m3 = tr3 + ti3;
      const m4 = tr4 + ti4, m5 = tr5 + ti5, m6 = tr6 + ti6, m7 = tr7 + ti7;
      const lo = (!(m0 <= lim) && !(m1 <= lim)) && (!(m2 <= lim) && !(m3 <= lim));
      const hi = (!(m4 <= lim) && !(m5 <= lim)) && (!(m6 <= lim) && !(m7 <= lim));
      if (lo && hi) return 0;
    }
  }
  let b = 0;
  if (tr0 + ti0 <= lim) b |= 128;
  if (tr1 + ti1 <= lim) b |= 64;
  if (tr2 + ti2 <= lim) b |= 32;
  if (tr3 + ti3 <= lim) b |= 16;
  if (tr4 + ti4 <= lim) b |= 8;
  if (tr5 + ti5 <= lim) b |= 4;
  if (tr6 + ti6 <= lim) b |= 2;
  if (tr7 + ti7 <= lim) b |= 1;
  return b;
}
