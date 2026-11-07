// xisto test: assert-based unit tests; the file passes when it exits cleanly with no leaks.
function clamp(x: int, lo: int, hi: int): int {
  return Math.max(lo, Math.min(hi, x));
}
assert(clamp(5, 0, 3) === 3, "clamp high");
assert(clamp(-2, 0, 3) === 0, "clamp low");
assert("a,b".split(",").length === 2);
const m: Map<string, int> = new Map<string, int>();
m.set("x", 1);
assert(m.has("x") && !m.has("y"), "map membership");
console.log("math ok");
