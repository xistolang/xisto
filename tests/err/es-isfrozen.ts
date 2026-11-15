// expect: Object.isFrozen is not supported
const o = Object.freeze({ a: 1 });
console.log(Object.isFrozen(o));
