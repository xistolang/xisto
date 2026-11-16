// Object.isFrozen of an object frozen through its name: true
const o = Object.freeze({ a: 1 });
console.log(Object.isFrozen(o));
