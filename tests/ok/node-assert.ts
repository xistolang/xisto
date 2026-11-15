// node:assert: the module called as a function, ok, (not)equal, (not)strictEqual, (not)deep(Strict)Equal, throws
// with a class / RegExp / validation function / object / message, doesNotThrow, rejects, doesNotReject, match,
// doesNotMatch, fail, ifError, AssertionError (code, operator, generatedMessage) with Node's messages: the short
// form, the stacked one (+ actual - expected) and the line diff of objects with skipped lines
import assert from "node:assert";
import * as A from "node:assert";
import { strictEqual, AssertionError } from "node:assert";

function show(f: () => void): void {
  try {
    f();
    console.log("ok");
  } catch (e) {
    if (e instanceof AssertionError) console.log(JSON.stringify(e.message), e.operator, e.generatedMessage, e.code, e.name);
    else console.log("other:", (e as Error).message);
  }
}

class Point {
  x: number;
  y: number;
  constructor(x: number, y: number) {
    this.x = x;
    this.y = y;
  }
}
class HttpError extends Error {
  status: number;
  constructor(message: string, status: number) {
    super(message);
    this.status = status;
  }
}

const n = 3;
show(() => assert(n > 2));
show(() => assert(n > 5));
show(() => assert.ok(n === 4));
show(() => A.ok(""));
show(() => assert.ok(0, "custom message"));
show(() => assert(false, "plain"));
show(() => assert.equal(1, 2));
show(() => assert.equal(1, "1"));
show(() => assert.equal(NaN, NaN));
show(() => assert.equal(1, 2, "m"));
show(() => assert.notEqual(1, 1));
show(() => assert.notEqual(1, 2));
show(() => strictEqual(1, 2));
show(() => assert.strictEqual("a", "b", "msg"));
show(() => assert.strictEqual("hello world", "hello there"));
show(() => assert.strictEqual("a".repeat(10), "b".repeat(10)));
show(() => assert.strictEqual(1, "1"));
show(() => assert.strictEqual(true, false));
show(() => assert.strictEqual(-0, 0));
show(() => assert.strictEqual(NaN, NaN));
show(() => assert.strictEqual(new Point(1, 2), new Point(1, 2)));
show(() => assert.notStrictEqual(3, 3));
show(() => assert.notStrictEqual("abcdefgh", "abcdefgh"));
show(() => assert.deepStrictEqual(new Point(1, 2), new Point(1, 2)));
show(() => assert.deepStrictEqual(new Point(1, 2), new Point(1, 3)));
show(() => assert.deepStrictEqual({ a: 1, b: [1, 2] }, { a: 1, b: [1, 3] }));
show(() => assert.deepStrictEqual([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], [1, 2, 3, 4, 5, 6, 7, 8, 9, 11]));
show(() => assert.deepStrictEqual({ name: "x", tags: ["a"] }, { name: "y", tags: ["a"] }));
show(() => assert.deepStrictEqual("a", "b"));
show(() => assert.deepStrictEqual(new Map([["k", 1]]), new Map([["k", 1]])));
show(() => assert.notDeepStrictEqual([1, 2], [1, 2]));
show(() => assert.notDeepStrictEqual(1, 1));
show(() => assert.deepEqual({ a: 1 }, { a: 2 }));
show(() => assert.notDeepEqual({ a: 1 }, { a: 1 }));
show(() => assert.throws(() => { throw new TypeError("bad"); }, TypeError));
show(() => assert.throws(() => { throw new TypeError("bad"); }, RangeError));
show(() => assert.throws(() => { throw new HttpError("nope", 404); }, HttpError));
show(() => assert.throws(() => {}));
show(() => assert.throws(() => {}, TypeError));
show(() => assert.throws(() => {}, TypeError, "msg"));
show(() => assert.throws(() => {}, "msg"));
show(() => assert.throws(() => { throw new Error("boom"); }, /bo+m/));
show(() => assert.throws(() => { throw new Error("boom"); }, /zzz/));
show(() => assert.throws(() => { throw new Error("boom"); }, { message: "boom" }));
show(() => assert.throws(() => { throw new Error("boom"); }, { message: "nope" }));
show(() => assert.throws(() => { throw new Error("boom"); }, { message: "boom", name: "TypeError" }));
show(() => assert.throws(() => { throw new TypeError("x"); }, (e: any) => e instanceof TypeError));
show(() => assert.throws(() => { throw new TypeError("x"); }, (e: any) => e instanceof RangeError));
show(() => assert.doesNotThrow(() => {}));
show(() => assert.doesNotThrow(() => { throw new Error("x"); }));
show(() => assert.doesNotThrow(() => { throw new Error("x"); }, "why"));
show(() => assert.doesNotThrow(() => { throw new Error("x"); }, TypeError, "why"));
show(() => assert.match("abc", /b/));
show(() => assert.match("abc", /x/));
show(() => assert.doesNotMatch("abc", /b/));
show(() => assert.fail("boom"));
show(() => assert.fail());
show(() => assert.ifError(null));
show(() => assert.ifError(new Error("e")));
const e = new assert.AssertionError({ actual: 1, expected: 2, operator: "strictEqual" });
console.log(e.message, e instanceof Error, e.name, e.code, e.generatedMessage);
const e2 = new AssertionError({ message: "mine", actual: 1, expected: 2, operator: "notEqual" });
console.log(e2.message, e2.generatedMessage, e2.operator);
await assert.rejects(async () => { throw new Error("r"); }, /r/);
await assert.rejects(async () => { throw new TypeError("t"); }, TypeError);
await assert.doesNotReject(async () => 1);
try {
  await assert.rejects(async () => 1);
} catch (err) {
  console.log((err as Error).message);
}
try {
  await assert.doesNotReject(async () => { throw new Error("late"); });
} catch (err) {
  console.log((err as Error).message);
}
console.log("done");
