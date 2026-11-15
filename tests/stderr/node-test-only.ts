// node:test's only: with test.only (or { only: true }) in a file, the other tests of that level do not run and are
// not reported, as Node 26 does without --test-only; a suite marked only runs all of its tests
import { test, describe, it } from "node:test";

test("not this one", () => {
  throw new Error("must not run");
});
test.only("this one", () => {});
test("this one too", { only: true }, () => {});
describe.only("this suite", () => {
  it("runs", () => {});
  it("runs too", () => {});
});
describe("other suite", () => {
  it("never", () => {
    throw new Error("must not run");
  });
});
