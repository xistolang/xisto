// expect: backreferences (\1) are not supported
const r = /(a)\1/;
console.log(r.test("aa"));
