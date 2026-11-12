// expect: nothing to repeat
const r = /x(?<=x)*/;
console.log(r.test("xyz"));
