// node:crypto: digest() with no encoding gives a Buffer, and createHmac takes a Buffer key (a key derived by another HMAC)
import { createHash, createHmac } from "node:crypto";
const key = createHmac("sha256", "secret").update("purpose").digest();
console.log(key.length, key.toString("hex"));
console.log(createHmac("sha256", key).update("payload").digest("base64url"));
console.log(createHash("sha1").update("abc").digest().toString("base64"));
const long = Buffer.alloc(100, 7);
console.log(createHmac("sha512", long).update("x").digest("hex").slice(0, 32));
