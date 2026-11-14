// node:crypto's webcrypto.subtle subset: digest, HMAC keys (importKey raw and jwk, exportKey raw), sign and verify,
// as jose-style code uses them; results compared through Buffer.from (Node gives ArrayBuffers).
import { createHmac, subtle, webcrypto } from "node:crypto";

async function main(): Promise<void> {
  const data = Buffer.from("payload to authenticate");
  for (const alg of ["SHA-1", "SHA-256", "SHA-384", "SHA-512"]) {
    const d = await subtle.digest(alg, data);
    console.log(alg, Buffer.from(d).toString("hex"));
  }
  console.log(Buffer.from(await webcrypto.subtle.digest({ name: "sha-256" }, Buffer.from("x"))).toString("base64"));
  const key = await subtle.importKey("raw", Buffer.from("my secret key"), { name: "HMAC", hash: "SHA-256" }, true, ["sign", "verify"]);
  console.log(key.type, key.extractable, key.algorithm.name, key.algorithm.hash.name, key.algorithm.length, key.usages.join(","));
  const sig = await subtle.sign("HMAC", key, data);
  console.log(Buffer.from(sig).toString("hex"), Buffer.from(sig).toString("hex") === createHmac("sha256", "my secret key").update(data).digest("hex"));
  console.log(await subtle.verify("HMAC", key, sig, data), await subtle.verify({ name: "HMAC" }, key, sig, Buffer.from("other")));
  console.log(Buffer.from(await subtle.exportKey("raw", key)).toString());
  const jwkKey = await subtle.importKey("jwk", { kty: "oct", k: "bXkgc2VjcmV0IGtleQ" }, { name: "HMAC", hash: { name: "SHA-512" } }, false, ["sign"]);
  console.log(jwkKey.algorithm.hash.name, jwkKey.algorithm.length, Buffer.from(await subtle.sign({ name: "HMAC" }, jwkKey, data)).toString("base64url"));
  try {
    await subtle.exportKey("raw", jwkKey);
  } catch (e) {
    console.log((e as Error).name);
  }
  try {
    await subtle.digest("MD5", data);
  } catch (e) {
    console.log((e as Error).name, (e as Error).message);
  }
  const r = webcrypto.getRandomValues(new Uint8Array(8));
  console.log(r.length, webcrypto.randomUUID().length);
}
main();
