// expect: a symbol key here must be a constant made by Symbol("description")
const d: string = "x";
const T = Symbol(d);
const o = { [T]: 1 };
console.log(o);
