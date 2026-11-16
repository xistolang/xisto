// XB-250: Date, Buffer, typed arrays and bigint live in the base runtime; using them, or importing a module whose
// unused functions need timers, fetch, coroutines or try, must not pull in the event loop and the cleanup stack
// (tests/run.sh checks the emitted C of this program for them).
import { twice } from "../size/looplib.ts";

function keyCount<T extends object>(o: T): number {
  return Object.keys(o).length;
}

function neverCalled(): void {
  setTimeout(() => console.log("never"), 1);
  const big = 2n ** 70n;
  console.log(big.toString(), new Float64Array(2).length);
}

const d = new Date(86400000);
console.log(d.getTime(), d.toISOString(), d.getUTCFullYear());
const b = Buffer.from("xisto");
console.log(b.length, b.toString("hex"), b.toString("base64"));
const u = new Uint8Array(4);
u[2] = 9;
console.log(u.length, u[2], u.join(","));
const f = new Float64Array(3);
f[1] = 1.5;
console.log(f.length, f[1]);
const big = 12345678901234567890n * 3n;
console.log(big.toString(), big.toString().length);
console.log(keyCount({ a: 1, b: "x", c: true }));
console.log(twice(21));
