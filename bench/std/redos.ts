// ReDoS: nested quantifiers on input that almost matches. Backtracking engines (V8, JSC) take time
// exponential in the input; xisto's RegExp is linear. 22 characters keeps Node and Bun in seconds.
let hits: int = 0;
for (let i: int = 0; i < 10; i++) {
  if (/^(a+)+$/.test("aaaaaaaaaaaaaaaaaaaaaa!")) {
    hits++;
  }
  if (/^(\w+\s?)*$/.test("an ordinary sentence that ends badly!")) {
    hits++;
  }
}
// a fixed prefix then an unbounded run that never ends in a match: trying each "abcd" with an anchored search
// would be quadratic (the prefilter of xisto's engine once did this; now it only hands positions to the DFA)
let t = "";
for (let i: int = 0; i < 20000; i++) t += "abcd";
const m = t.match(/abcd[^x]*y/g);
if (m !== null) hits++;
console.log("hits", hits);
