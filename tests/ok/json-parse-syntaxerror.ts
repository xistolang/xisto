// XB-205: `instanceof SyntaxError` with no other mention of the class compiles; a JSON.parse failure is a SyntaxError
// (instanceof true, name and V8's message with its position)
interface T { a: int }
const cases = ["{", "", "[1,", "[1 2]", '{"a" 1}', '{"a":1 "b":2}', '{"a":1,}', "[1,]", "abc", "tru", '"abc', '"a\\x"',
  '"\\u12G4"', "1 2", "-", "1.", "1e", "01", '{"a":1}x', '"a\nb"', "{\n\"a\": 1,\n\"b\" 2}", "undefined", "[tru]",
  "[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,x]",
  "[1, 2, 3, 4, x, 5, 6, 7, 8, 9]", "[\"é\", x]", "{\"é\": 1 2}", "0x10", "-01", "1.5e", '{"a":1}'];
for (const c of cases) {
  try {
    const t: T = JSON.parse(c);
    console.log("ok", t.a);
  } catch (e) {
    console.log(e instanceof SyntaxError, e instanceof Error, (e as Error).name, (e as Error).message);
  }
}
const nums: number[] = JSON.parse("[1e5, -0.5e-3, 1E+2, 0, -0]");
console.log(nums);
try {
  throw new RangeError("mine");
} catch (e) {
  console.log(e instanceof RangeError, e instanceof SyntaxError, (e as Error).message);
}
