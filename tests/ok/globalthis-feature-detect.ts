console.log(typeof globalThis.WebAssembly, typeof globalThis.Iterator, typeof globalThis.Float16Array, typeof globalThis.FinalizationRegistry, typeof globalThis.escape, typeof globalThis.PerformanceObserver, typeof globalThis.nope);
console.log("WebAssembly" in globalThis, "undefined" in globalThis, "nope" in globalThis, "fetch" in globalThis);
globalThis.queueMicrotask(() => console.log("micro"));
console.log("sync");
