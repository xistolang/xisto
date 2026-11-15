// xisto:assert/strict (node:assert/strict, also import { strict } from "node:assert"): node:assert with the strict
// comparisons under the loose names (equal is strictEqual, deepEqual is deepStrictEqual...), written in xisto over
// runtime/node/assert.ts.

import { strictEqual, notStrictEqual, deepStrictEqual, notDeepStrictEqual } from "xisto:assert";

export {
  ok,
  strictEqual,
  notStrictEqual,
  deepStrictEqual,
  notDeepStrictEqual,
  throws,
  __throwsMsg,
  __throwsRe,
  __throwsFn,
  __throwsClass,
  doesNotThrow,
  __doesNotThrowClass,
  __doesNotThrowRe,
  rejects,
  __rejectsMsg,
  __rejectsRe,
  __rejectsFn,
  __rejectsClass,
  doesNotReject,
  __doesNotRejectClass,
  __doesNotRejectRe,
  match,
  doesNotMatch,
  fail,
  ifError,
  AssertionError,
  AssertionErrorOptions,
} from "xisto:assert";

/** strictEqual. */
export function equal(actual: any, expected: any, message: string | Error | null = null): void {
  strictEqual(actual, expected, message);
}
/** notStrictEqual. */
export function notEqual(actual: any, expected: any, message: string | Error | null = null): void {
  notStrictEqual(actual, expected, message);
}
/** deepStrictEqual. */
export function deepEqual(actual: any, expected: any, message: string | Error | null = null): void {
  deepStrictEqual(actual, expected, message);
}
/** notDeepStrictEqual. */
export function notDeepEqual(actual: any, expected: any, message: string | Error | null = null): void {
  notDeepStrictEqual(actual, expected, message);
}
