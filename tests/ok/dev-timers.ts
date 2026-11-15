// XB-203: timers in a --dev build (the runtime as a library, built without worker threads): setTimeout,
// setInterval and clearInterval were declared only for programs with threads, so every --dev build with a timer failed
let n = 0;
const h = setInterval(() => { n++; console.log("tick", n); if (n === 3) clearInterval(h); }, 5);
setTimeout(() => console.log("once"), 1);
console.log("sync end");
async function later(): Promise<string> { return "x"; }
later().then((v) => console.log("then", v));
console.log("after then");
