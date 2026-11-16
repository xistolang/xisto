// expect: an any is not indexed by a symbol: its keys are names
const k = Symbol("k");
const o: any = {};
o[k] = 1;
