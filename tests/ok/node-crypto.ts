// node:crypto and crypto: createHash over md5/sha1/sha256/sha512 (chained update, hex/base64/base64url), createHmac,
// randomUUID's shape and the errors, against Node's output
import { createHash, createHmac, randomUUID, getHashes } from "node:crypto";
import { createHash as createHash2 } from "crypto";

const algs = ["md5", "sha1", "sha256", "sha512"];
const inputs = ["", "abc", "a".repeat(1000), "ação ç 日本 €", "The quick brown fox jumps over the lazy dog"];
for (const a of algs) {
  for (const s of inputs) console.log(a, s.length, createHash(a).update(s).digest("hex"));
  console.log(a, "chained", createHash(a).update("The quick ").update("brown fox").digest("base64"));
  console.log(a, "url", createHash(a).update("??>>??").digest("base64url"));
  console.log(a, "hmac", createHmac(a, "key").update("The quick brown fox jumps over the lazy dog").digest("hex"));
  console.log(a, "hmac long key", createHmac(a, "k".repeat(200)).update("data").digest("hex"));
}
console.log(createHash2("sha256").update("x").digest("hex"));
console.log(getHashes().length > 0);
const ids: string[] = [];
for (let i = 0; i < 200; i++) ids.push(randomUUID());
const shape = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
console.log(ids.every((u) => shape.test(u)), new Set<string>(ids).size);
try {
  createHash("sha3-999");
} catch (e) {
  console.log((e as Error).message);
}
const h = createHash("sha256");
h.update("once");
h.digest("hex");
try {
  h.update("again");
} catch (e) {
  console.log((e as Error).message);
}
