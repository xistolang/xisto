const a: unknown = [1, 2];
const b: unknown = 'x';
console.log(Array.isArray(a), Array.isArray(b), Array.isArray(null as unknown));
