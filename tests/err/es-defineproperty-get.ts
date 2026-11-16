// expect: Object.defineProperty with get/set is not supported: a property computed by a function is a class accessor here
const r: Record<string, number> = {};
Object.defineProperty(r, "k", { get: () => 1 });
console.log(r);
