// expect: Object.defineProperty with get/set is not supported: a property computed by a function is a class accessor here
const o: { v: number } = { v: 1 };
let store = 0;
Object.defineProperty(o, "w", { get() { return store; }, set(v: number) { store = v; } });
