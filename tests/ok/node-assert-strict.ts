// node:assert/strict (and import { strict as assert } from "node:assert"): equal is strictEqual, deepEqual is
// deepStrictEqual...; ok and the module called as a function; named imports (ok(x) too)
import assert from "node:assert/strict";
import { strict as sa } from "node:assert";
import * as S from "node:assert/strict";
import { ok, deepEqual } from "node:assert/strict";

function show(f: () => void): void {
  try {
    f();
    console.log("ok");
  } catch (e) {
    const x: any = e;
    console.log(JSON.stringify(x.message), x.operator, x.generatedMessage);
  }
}
const v = 2;
show(() => assert(v === 3));
show(() => assert.equal(1, 1));
show(() => assert.equal(1, 2));
show(() => assert.notEqual(1, 1));
show(() => assert.deepEqual({ a: 1 }, { a: 1 }));
show(() => assert.deepEqual({ a: 1, b: { c: [1, 2] } }, { a: 1, b: { c: [1, 2, 3] } }));
show(() => assert.notDeepEqual([1], [1]));
show(() => sa.equal("x", "y"));
show(() => sa(v > 10));
show(() => sa.ok(v > 1));
show(() => S.strictEqual(v, 2));
show(() => S.deepStrictEqual([v], [3]));
show(() => ok(v < 0));
show(() => deepEqual([1, 2], [2, 1]));
show(() => assert.throws(() => { throw new RangeError("r"); }, RangeError));
show(() => assert.throws(() => { throw new RangeError("r"); }, { name: "RangeError", message: "r" }));
show(() => assert.match("strict", /ict$/));
console.log(new assert.AssertionError({ actual: [1], expected: [2], operator: "deepStrictEqual" }).message);
