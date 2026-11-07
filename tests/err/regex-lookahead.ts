// expect: lookahead ((?= and (?!) is not supported
const r = /x(?=y)/;
console.log(r.test("xy"));
