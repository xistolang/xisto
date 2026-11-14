// node:crypto Ed25519: keys from PKCS#8/SPKI/JWK and back, signatures (deterministic: Node's bytes), verify,
// generateKeyPairSync("ed25519") with and without encodings, and the errors; JWK field order of EC and RSA keys.
import * as crypto from "node:crypto";
const seed = Buffer.alloc(32, 9);
const k = crypto.createPrivateKey({ key: Buffer.concat([Buffer.from("302e020100300506032b657004220420", "hex"), seed]), format: "der", type: "pkcs8" });
const jwk = k.export({ format: "jwk" });
console.log(jwk.kty, jwk.crv, jwk.x, jwk.d, k.asymmetricKeyType);
const ec = crypto.generateKeyPairSync("ec", { namedCurve: "P-256" }).privateKey.export({ format: "jwk" });
console.log(JSON.stringify(ec).match(/"[a-z]+":/g)!.join(""), JSON.stringify(ec).length);
const rsa = crypto.generateKeyPairSync("rsa", { modulusLength: 512 }).privateKey.export({ format: "jwk" });
console.log(JSON.stringify(rsa).match(/"[a-z]+":/g)!.join(""));
console.log(crypto.sign(null, Buffer.from("abc"), k).toString("hex"));
const pub = crypto.createPublicKey(k);
console.log(pub.export({ type: "spki", format: "pem" }));
console.log(k.export({ type: "pkcs8", format: "pem" }));
console.log(crypto.verify(null, Buffer.from("abc"), pub, crypto.sign(null, Buffer.from("abc"), k)), crypto.verify(null, Buffer.from("abd"), pub, crypto.sign(null, Buffer.from("abc"), k)));
for (let i = 0; i < 20; i++) {
  const p = crypto.generateKeyPairSync("ed25519");
  const m = crypto.randomBytes(i * 7);
  const s = crypto.sign(null, m, p.privateKey);
  if (!crypto.verify(null, m, p.publicKey, s)) console.log("FAIL", i);
}
const long = Buffer.alloc(5000, 0x61);
console.log(crypto.sign(null, long, k).toString("base64"));
try { crypto.createSign("sha256").update("x").sign(k); } catch (e: any) { console.log(e.code, e.message); }
try { crypto.sign("sha256", Buffer.from("x"), k); } catch (e: any) { console.log(e.code, e.message); }
const pem = crypto.generateKeyPairSync("ed25519", { publicKeyEncoding: { type: "spki", format: "pem" }, privateKeyEncoding: { type: "pkcs8", format: "pem" } });
console.log(pem.publicKey.length, pem.privateKey.length);
const j = crypto.createPrivateKey({ key: k.export({ format: "jwk" }), format: "jwk" });
console.log(j.equals(k), crypto.createPublicKey({ key: pub.export({ format: "jwk" }), format: "jwk" }).equals(pub));
