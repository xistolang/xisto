// expect: an any is not indexed by a symbol: its keys are names
const K = Symbol("k");
const t = { [K]: 5 };
const a: any = t;
console.log(a[K]);
