// expect: invalid named reference: no group of that name
const r = /(?<a>x)\k<b>/;
console.log(r.test("xx"));
