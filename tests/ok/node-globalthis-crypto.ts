// globalThis: the standard globals through it (functions, objects, typeof and "in" feature checks, typeof of other
// runtimes' globals); the global crypto (Web Crypto, as in Node 20+): randomUUID, getRandomValues, subtle.digest,
// subtle HMAC keys; node:crypto's webcrypto is the same object
import { webcrypto } from "node:crypto";
const id = crypto.randomUUID();
console.log(id.length, /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(id));
const bytes = crypto.getRandomValues(new Uint8Array(16));
console.log(bytes.length, globalThis.crypto.randomUUID().length, webcrypto === globalThis.crypto);
const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode("hello"));
console.log(Buffer.from(digest).toString("hex"));
for (const alg of ["SHA-1", "SHA-384", "SHA-512"]) {
  const d = await globalThis.crypto.subtle.digest(alg, new TextEncoder().encode("abc"));
  console.log(alg, new Uint8Array(d).length, Buffer.from(d).toString("hex").slice(0, 16));
}
const key = await crypto.subtle.importKey("raw", new TextEncoder().encode("secret"), { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
const sig = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode("msg"));
console.log(Buffer.from(sig).toString("base64"), await crypto.subtle.verify("HMAC", key, sig, new TextEncoder().encode("msg")));
console.log(typeof globalThis.setTimeout, typeof globalThis.Deno, typeof globalThis.process, typeof window, typeof globalThis);
console.log("fetch" in globalThis, "Bun" in globalThis, typeof globalThis.crypto !== "undefined");
globalThis.setTimeout(() => console.log("timer"), 1);
console.log(globalThis.Math.max(1, 2), globalThis.process.platform.length > 0, globalThis.JSON.stringify([1]));
globalThis.console.log("via globalThis");
const t0 = globalThis.performance.now(); console.log(t0 >= 0);
const isNode = typeof globalThis.process !== "undefined" && typeof window === "undefined";
const isDeno = typeof globalThis.Deno !== "undefined";
const isBun = "Bun" in globalThis;
console.log(isNode, isDeno, isBun, typeof globalThis.structuredClone, typeof globalThis.undefined, typeof document, typeof self);
const enc = new globalThis.TextEncoder().encode("x");
console.log(enc.length, globalThis.Number.isInteger(3), globalThis.String(12), globalThis.parseInt("42"));
