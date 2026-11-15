// expect: the replacer of JSON.stringify cannot be number
const s: string = JSON.stringify([1, 2], 5 as number);
console.log(s);
