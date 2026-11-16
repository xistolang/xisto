// node:test run as a plain program, every test passing: Node's spec reporter ("✔ name (1ms)", "▶ suite", skip and
// todo marks, diagnostics, the "ℹ tests" summary; durations normalized by the runner), hooks in Node's order,
// async tests, subtests, describe/it, the skip/todo/only forms and options, exit status 0
import test, { describe, it, before, after, beforeEach, afterEach } from "node:test";
import assert from "node:assert/strict";

const order: string[] = [];
console.log("module body");

test("adds", () => {
  assert.equal(1 + 1, 2);
});
test("async", async () => {
  const v = await Promise.resolve(41);
  assert.equal(v + 1, 42);
});
test("with options", { timeout: 1000 }, () => {
  assert.ok(true);
});
test("skipped by option", { skip: true }, () => {
  throw new Error("never runs");
});
test("skipped with a reason", { skip: "not on this platform" }, () => {});
test.skip("test.skip", () => {});
test.todo("a todo");
test("todo by option", { todo: "later" }, () => {});
test("diagnostics", (t) => {
  t.diagnostic(`running ${t.name}`);
  t.diagnostic("second line");
});
test("skips itself", (t) => {
  t.skip("decided at run time");
});
test("subtests", async (t) => {
  await t.test("first", () => {
    assert.deepEqual([1, 2], [1, 2]);
  });
  await t.test("second", async () => {
    await Promise.resolve();
  });
});

describe("a suite", () => {
  before(() => order.push("before"));
  after(() => order.push("after"));
  beforeEach((t) => order.push("beforeEach " + t.name));
  afterEach((t) => order.push("afterEach " + t.name));
  it("one", () => {
    order.push("one");
  });
  it("two", () => {
    order.push("two");
  });
  describe("nested", () => {
    beforeEach(() => order.push("inner beforeEach"));
    it("three", () => {
      order.push("three");
    });
  });
  it.skip("skipped it", () => {});
  it.todo("todo it");
});
describe.skip("skipped suite", () => {
  it("never", () => {});
});

test("hooks ran in order", () => {
  assert.deepEqual(order, [
    "before",
    "beforeEach one",
    "one",
    "afterEach one",
    "beforeEach two",
    "two",
    "afterEach two",
    "beforeEach three",
    "inner beforeEach",
    "three",
    "afterEach three",
    "beforeEach todo it",
    "afterEach todo it",
    "after",
  ]);
});
console.log("end of module body");
