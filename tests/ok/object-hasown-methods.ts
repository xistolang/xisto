class P { x: number = 1; m(): number { return 2; } get g(): number { return 3; } }
const p = new P();
console.log(Object.hasOwn(p, "x"), Object.hasOwn(p, "m"), Object.hasOwn(p, "g"), Object.hasOwn(p, "z"));
console.log(Object.prototype.hasOwnProperty.call(p, "m"), "m" in p, "g" in p);
