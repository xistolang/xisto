// node:test with failing tests: "✖" lines, the failing parent of a failing subtest, a failing beforeEach hook, a
// todo that throws (listed, not counted as a failure), the "failing tests" section (xisto's stack frames: the
// expected stdout is xisto's), and exit status 1 (process.exitCode)
import { test, describe, it, beforeEach } from "node:test";
import assert from "node:assert";

test("passes", () => {});
test("strictEqual fails", () => {
  assert.strictEqual(1 + 1, 3);
});
test("throws an Error", () => {
  throw new Error("boom");
});
test("rejects", async () => {
  await Promise.resolve();
  throw new TypeError("async boom");
});
test("parent", async (t) => {
  await t.test("good child", () => {});
  await t.test("bad child", () => {
    assert.deepStrictEqual({ a: [1, 2] }, { a: [1, 3] });
  });
});
describe("suite with a bad hook", () => {
  beforeEach(() => {
    throw new Error("hook failed");
  });
  it("cannot run", () => {});
});
test("todo that fails", { todo: "not done" }, () => {
  throw new Error("expected for now");
});
class Problem {
  code: number = 7;
}
test("non-error thrown", () => {
  throw new Problem();
});
