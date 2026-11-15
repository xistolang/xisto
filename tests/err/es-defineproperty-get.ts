// expect: 'get' in a property descriptor is not supported
const r: Record<string, number> = {};
Object.defineProperty(r, "k", { get: () => 1 });
console.log(r);
