// expect: lookahead ((?= and (?!) of more than one character is not supported
const r = /x(?=yz)/;
console.log(r.test("xyz"));
