// expect: invalid regular expression flags
const r = /x/gg;
console.log(r.test("x"));
