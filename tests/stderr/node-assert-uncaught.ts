// a failed assertion nobody catches: the AssertionError is printed (xisto's format: "uncaught AssertionError: ..."
// and the stack; Node prints the error object) and the program ends with status 1
import assert from "node:assert";
console.log("before");
assert.strictEqual(1 + 1, 3);
console.log("never");
