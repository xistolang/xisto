/**
 * Work split over the cores of the machine, with the results in the order a plain loop would give them.
 *
 * Each function is the parallel twin of something JavaScript already has: {@link parallelMap} is
 * `items.map(fn)`, {@link parallelFrom} is `Array.from({ length: count }, (_, i) => fn(i))`, {@link parallelFill}
 * is a `for` loop filling an array, and {@link parallelReduce} is `map` followed by `reduce`. The calls of `fn`
 * may run at the same time on different cores, but what the program sees is exactly what the sequential loop
 * would produce: results stay in index order, and a reduction combines them from the first to the last.
 *
 * @remarks
 * Compatibility: this file is the module itself. In Node it is the reference implementation, a plain loop on one
 * thread (run with `node --import ./packages/xisto/register.mjs program.ts`), so the output is the same
 * everywhere. The xisto compiler runs `fn` on a pool of threads, one per core (`XISTO_THREADS=N` sets how many),
 * when it can prove that the calls are independent:
 *
 * - `fn` must be written at the call: an arrow, or the name of a function.
 * - What it shares with the rest of the program (what it captures, and globals) it may only read: numbers and
 *   booleans, and arrays of them or typed arrays as `a[i]` and `a.length`, also through the functions it calls.
 * - It may build anything of its own (objects, arrays, strings) and change it, and call its methods.
 * - It must not assign a captured variable or a global, write into a shared array, print, use `Math.random`,
 *   timers or the network, or call a function value or another package's function.
 *
 * When it cannot prove that, the compiler says why in a warning and runs the plain loop, with the same output.
 * An error thrown by `fn` reaches the caller as in the loop: the one of the lowest index, after everything the
 * loop would have done before it.
 *
 * @example
 * ```ts
 * import { parallelFrom } from "xisto:parallel";
 * const squares = parallelFrom(5, (i) => i * i);
 * console.log(squares.join(" ")); // "0 1 4 9 16"
 * ```
 *
 * @packageDocumentation
 * @module xisto:parallel
 */

/**
 * Maps every item of an array to a new value, like `items.map(fn)`, with the calls spread over the cores.
 *
 * @param items - The array to map. It is not changed.
 * @param fn - Makes the value of one item; it gets the item and its index.
 * @returns A new array with `fn(items[i], i)` at index `i`.
 * @throws What `fn` throws: the error of the lowest index whose call fails.
 *
 * @remarks
 * Compatibility: in Node, `items.map(fn)` on one thread. In xisto, the calls run on a pool of threads when `fn`
 * is independent (see the module) and the items are numbers or booleans (for other items, map their indices with
 * {@link parallelFrom}); the array is the same.
 *
 * @example
 * ```ts
 * import { parallelMap } from "xisto:parallel";
 * const halves = parallelMap([2, 4, 6], (x) => x / 2);
 * console.log(halves.join(",")); // "1,2,3"
 * ```
 */
export function parallelMap<T, R>(items: T[], fn: (item: T, index: int) => R): R[] {
  const out: R[] = [];
  for (let i = 0; i < items.length; i++) out.push(fn(items[i], i));
  return out;
}

/**
 * Makes an array of `count` values, like `Array.from({ length: count }, (_, i) => fn(i))`, with the calls
 * spread over the cores.
 *
 * @param count - How many values; 0 gives an empty array.
 * @param fn - Makes the value at one index.
 * @returns A new array with `fn(i)` at index `i`, for `i` from 0 to `count - 1`.
 * @throws What `fn` throws: the error of the lowest index whose call fails.
 * @throws Error when `count` is negative.
 *
 * @remarks
 * Compatibility: in Node, a `for` loop on one thread. In xisto, the calls run on a pool of threads when `fn` is
 * independent (see the module); the array is the same.
 *
 * @example
 * ```ts
 * import { parallelFrom } from "xisto:parallel";
 * const rows = parallelFrom(3, (y) => `row ${y}`);
 * console.log(rows.join("; ")); // "row 0; row 1; row 2"
 * ```
 */
export function parallelFrom<R>(count: int, fn: (index: int) => R): R[] {
  if (count < 0) throw new Error(`parallelFrom: count must not be negative, got ${count}`);
  const out: R[] = [];
  for (let i = 0; i < count; i++) out.push(fn(i));
  return out;
}

/**
 * Sets every element of an array to the value `fn` makes for its index, with the calls spread over the cores.
 *
 * The parallel form of `for (let i = 0; i < array.length; i++) array[i] = fn(i)`. `fn` must not read `array`
 * itself: the elements change while it runs.
 *
 * @param array - The array to fill, in place. Its length does not change.
 * @param fn - Makes the value at one index.
 * @throws What `fn` throws: the error of the lowest index whose call fails; the elements before that index are
 * set and the others keep their values, as in the loop.
 *
 * @remarks
 * Compatibility: in Node, the `for` loop on one thread. In xisto, the calls run on a pool of threads when `fn` is
 * independent (see the module); the array ends the same.
 *
 * @example
 * ```ts
 * import { parallelFill } from "xisto:parallel";
 * const v: number[] = [0, 0, 0, 0];
 * parallelFill(v, (i) => i / 2);
 * console.log(v.join(" ")); // "0 0.5 1 1.5"
 * ```
 */
export function parallelFill<T>(array: T[], fn: (index: int) => T): void {
  for (let i = 0; i < array.length; i++) array[i] = fn(i);
}

/**
 * Makes `count` values and combines them into one, like `parallelFrom(count, fn).reduce(combine, initial)`, with
 * the calls of `fn` spread over the cores.
 *
 * The values are combined in index order, starting from `initial`: `combine(combine(initial, fn(0)), fn(1))`
 * and so on, so `combine` does not need to be commutative or associative and the result is always the same.
 *
 * @param count - How many values; 0 gives `initial`.
 * @param fn - Makes the value at one index; these calls run in parallel.
 * @param combine - Adds one value to the total so far; these calls run one after the other, in index order.
 * @param initial - The total before any value.
 * @returns The total after the value of the last index.
 * @throws What `fn` or `combine` throws: for `fn`, the error of the lowest index whose call fails.
 * @throws Error when `count` is negative.
 *
 * @remarks
 * Compatibility: in Node, a `for` loop on one thread. In xisto, the calls of `fn` run on a pool of threads when
 * `fn` is independent (see the module), and `combine` runs afterwards on the calling thread; the result is the
 * same.
 *
 * @example
 * ```ts
 * import { parallelReduce } from "xisto:parallel";
 * const sum = parallelReduce(4, (i) => i * 10, (total, x) => total + x, 0);
 * console.log(sum); // 60
 * ```
 */
export function parallelReduce<R>(count: int, fn: (index: int) => R, combine: (total: R, value: R) => R, initial: R): R {
  if (count < 0) throw new Error(`parallelReduce: count must not be negative, got ${count}`);
  let total = initial;
  for (let i = 0; i < count; i++) total = combine(total, fn(i));
  return total;
}
