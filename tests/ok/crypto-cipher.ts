// node:crypto: timingSafeEqual, randomInt, AES-CBC (createCipheriv), RSA PKCS#1 v1.5 signatures (createSign) and
// publicEncrypt (PKCS#1 v1.5 and OAEP: random padding, so the length and a decryption check are printed). The key is
// a throwaway test key made for this file.
import { constants, createCipheriv, createHash, createSign, publicEncrypt, randomInt, timingSafeEqual } from "node:crypto";

const PUB = [
  "-----BEGIN PUBLIC KEY-----",
  "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAmoDukWwpvoIi7j1Tfx9W",
  "OIFTwpuybYOZc0pQRQvDKgaIPr3NlKsIvAzbtnu1/ateWbeAODCsK7w91t3rna1v",
  "V22LOc4xBDcV22wdglYcR7rH+dgAtznDnpXYmQH3J61vOqvYtzgly08HQI7QXr9+",
  "eDipbhlGSadtvgNhSkxVAfyv3/VyVNUOMhoF7svSZR2BeVp8XyhxHYOKZLAUHhZf",
  "emfMtfyuPqrluOzumv23M9gIzeQCfxlgbcdQJWewoyNOJqjcZJdIKr0d0FSGGE00",
  "BXTiEqYXI6lMQATqXkDG6m8T09k9mzjRlSqzETd+1eRofmit+Crk1E28nOnIn22s",
  "hwIDAQAB",
  "-----END PUBLIC KEY-----"
].join("\n");
const PRIV1 = [
  "-----BEGIN RSA PRIVATE KEY-----",
  "MIIEogIBAAKCAQEAmoDukWwpvoIi7j1Tfx9WOIFTwpuybYOZc0pQRQvDKgaIPr3N",
  "lKsIvAzbtnu1/ateWbeAODCsK7w91t3rna1vV22LOc4xBDcV22wdglYcR7rH+dgA",
  "tznDnpXYmQH3J61vOqvYtzgly08HQI7QXr9+eDipbhlGSadtvgNhSkxVAfyv3/Vy",
  "VNUOMhoF7svSZR2BeVp8XyhxHYOKZLAUHhZfemfMtfyuPqrluOzumv23M9gIzeQC",
  "fxlgbcdQJWewoyNOJqjcZJdIKr0d0FSGGE00BXTiEqYXI6lMQATqXkDG6m8T09k9",
  "mzjRlSqzETd+1eRofmit+Crk1E28nOnIn22shwIDAQABAoIBAAMYTUi0dfgCLowl",
  "kyQ0whfK32ncKV+jbZ6vNAE63xbBOSSomkZw6dfg5tw2iWRV20OVnW5PhONk/e6H",
  "IehV4dxKexBJXrBkhuRb7t6X6WRQC+yntKJdXqgw/QnAZ4cJiygNt5jT+YEJ422z",
  "od23NrCJ0gfWkcXf7x7WfbIGg9CdizgG6VEGLdAL0Qhv7iZs+hS2yAuLKFNGC4hb",
  "BeZoDB2KCSP8MLkzcZ62G0/XdLEcLPCncoCSwve4tA9mLUYfR4EtChfbnDCPffDC",
  "pfLxjAdJNuLZ8Ou+g5nqAHazJsV3ytHtvUzjhxgJMPJlEENwIRpvHpYYgc8EtUfh",
  "H8Uv8gECgYEA0Et9dVA2nSEzZVhDcdq/8ZZdn41YgMZnMZyecbcZ9IDjXuTWnmr/",
  "comtDG8QaKXW6dluGEMuiMwlTYg9nUOqN4oU29Eys706aBC9kOhjIasgKsDgtgvY",
  "wl3XHNsXTvXxjlZqmMSk/r6QxaczZr9xgcCVxRiVnrMpg/PrfDxqWEcCgYEAveOd",
  "Uq0LPwX5muwlb16LBBc8hQE8isbQc01DvUSZBUOC0HwGGWI5LJAuLDRvdwFG7Bek",
  "QZdmfFqPVR6FrIJPRkJ1aYviduj4eNu2GRG/hwlHgBTsvH5rG9F8bq6bED+mzlHM",
  "ZUz6UhC8DmlP3GWfGbjxwVLjwwYhWt24ojSUacECgYA4U1GXuZsrHmFBlH6YeSjS",
  "cXDbxmMaX4xkNA/8fCD+pyPU81i9+f0dwfETnaz4P3BASFWnJfOddmbX8nGG51dO",
  "MX62enmVdSQoI+6/ErhL0HSgWwvo7ocs+CdFXFGPPp6VePTWXsanRGztpesWa8qL",
  "5cIaTC2ZfqVJU1pofAXd3QKBgGoEYNu9/V9siJV0btolINxh1Mo1jIW2xr75Tm6n",
  "k/+aV7jq5cTaUC2POhg4FluM/PnnoS91xiR7szbE2cEc6AHELSWGkiPtonxnvt10",
  "SUitZ5wdEqeStV5MsXpiRfTmdM0QYlvUhJW4/iXVql7U5eVImsFi9AL4szq2O9Vk",
  "IdCBAoGAHzPIaG71n0G6dldDcgoPdYNwh5EBAcYcU+kdH6A/HM8vB3XYZ9bCyF1X",
  "c/tzX61sWAYkQj9+6aGaEyF7hz+/lYSzxQ84k2KicGMCRVpmfD3S9gHx6iAj5ELC",
  "AC8GnrTOvsqzfvgsSG0l7hASIG39Dho0LniwxjXUAhuKPkwES24=",
  "-----END RSA PRIVATE KEY-----"
].join("\n");
const PRIV8 = [
  "-----BEGIN PRIVATE KEY-----",
  "MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQCagO6RbCm+giLu",
  "PVN/H1Y4gVPCm7Jtg5lzSlBFC8MqBog+vc2Uqwi8DNu2e7X9q15Zt4A4MKwrvD3W",
  "3eudrW9XbYs5zjEENxXbbB2CVhxHusf52AC3OcOeldiZAfcnrW86q9i3OCXLTwdA",
  "jtBev354OKluGUZJp22+A2FKTFUB/K/f9XJU1Q4yGgXuy9JlHYF5WnxfKHEdg4pk",
  "sBQeFl96Z8y1/K4+quW47O6a/bcz2AjN5AJ/GWBtx1AlZ7CjI04mqNxkl0gqvR3Q",
  "VIYYTTQFdOISphcjqUxABOpeQMbqbxPT2T2bONGVKrMRN37V5Gh+aK34KuTUTbyc",
  "6cifbayHAgMBAAECggEAAxhNSLR1+AIujCWTJDTCF8rfadwpX6Ntnq80ATrfFsE5",
  "JKiaRnDp1+Dm3DaJZFXbQ5Wdbk+E42T97och6FXh3Ep7EElesGSG5Fvu3pfpZFAL",
  "7Ke0ol1eqDD9CcBnhwmLKA23mNP5gQnjbbOh3bc2sInSB9aRxd/vHtZ9sgaD0J2L",
  "OAbpUQYt0AvRCG/uJmz6FLbIC4soU0YLiFsF5mgMHYoJI/wwuTNxnrYbT9d0sRws",
  "8KdygJLC97i0D2YtRh9HgS0KF9ucMI998MKl8vGMB0k24tnw676DmeoAdrMmxXfK",
  "0e29TOOHGAkw8mUQQ3AhGm8elhiBzwS1R+EfxS/yAQKBgQDQS311UDadITNlWENx",
  "2r/xll2fjViAxmcxnJ5xtxn0gONe5Naeav9yia0MbxBopdbp2W4YQy6IzCVNiD2d",
  "Q6o3ihTb0TKzvTpoEL2Q6GMhqyAqwOC2C9jCXdcc2xdO9fGOVmqYxKT+vpDFpzNm",
  "v3GBwJXFGJWesymD8+t8PGpYRwKBgQC9451SrQs/Bfma7CVvXosEFzyFATyKxtBz",
  "TUO9RJkFQ4LQfAYZYjkskC4sNG93AUbsF6RBl2Z8Wo9VHoWsgk9GQnVpi+J26Ph4",
  "27YZEb+HCUeAFOy8fmsb0XxurpsQP6bOUcxlTPpSELwOaU/cZZ8ZuPHBUuPDBiFa",
  "3biiNJRpwQKBgDhTUZe5myseYUGUfph5KNJxcNvGYxpfjGQ0D/x8IP6nI9TzWL35",
  "/R3B8ROdrPg/cEBIVacl8512ZtfycYbnV04xfrZ6eZV1JCgj7r8SuEvQdKBbC+ju",
  "hyz4J0VcUY8+npV49NZexqdEbO2l6xZryovlwhpMLZl+pUlTWmh8Bd3dAoGAagRg",
  "2739X2yIlXRu2iUg3GHUyjWMhbbGvvlObqeT/5pXuOrlxNpQLY86GDgWW4z8+eeh",
  "L3XGJHuzNsTZwRzoAcQtJYaSI+2ifGe+3XRJSK1nnB0Sp5K1XkyxemJF9OZ0zRBi",
  "W9SElbj+JdWqXtTl5UiawWL0AvizOrY71WQh0IECgYAfM8hobvWfQbp2V0NyCg91",
  "g3CHkQEBxhxT6R0foD8czy8Hddhn1sLIXVdz+3NfrWxYBiRCP37poZoTIXuHP7+V",
  "hLPFDziTYqJwYwJFWmZ8PdL2AfHqICPkQsIALwaetM6+yrN++CxIbSXuEBIgbf0O",
  "GjQueLDGNdQCG4o+TARLbg==",
  "-----END PRIVATE KEY-----"
].join("\n");

const a = Buffer.from("same-signature");
console.log(timingSafeEqual(a, Buffer.from("same-signature")), timingSafeEqual(a, Buffer.from("same-signaturX")));
try {
  timingSafeEqual(a, Buffer.from("short"));
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}
let inRange = true;
for (let i = 0; i < 200; i++) {
  const n = randomInt(0, 10);
  if (n < 0 || n > 9) inRange = false;
}
const m = randomInt(5);
console.log("randomInt in range:", inRange, m >= 0 && m < 5);

const key = Buffer.alloc(16);
for (let i = 0; i < 16; i++) key[i] = 0x30 + (i % 10);
const iv = Buffer.from("AAAAAAAAAAAAAAAA", "utf8");
for (const text of ["", "x", "exactly 16 bytes", "<enviBio><dado>ção çã 123</dado></enviBio>".repeat(5)]) {
  const cipher = createCipheriv("aes-128-cbc", key, iv);
  const enc = Buffer.concat([cipher.update(text, "utf8"), cipher.final()]);
  console.log(enc.length, enc.toString("base64"));
}
const k32 = Buffer.alloc(32);
for (let i = 0; i < 32; i++) k32[i] = i;
const c2 = createCipheriv("aes-256-cbc", k32, iv);
console.log(Buffer.concat([c2.update("aes-256 text", "utf8"), c2.final()]).toString("hex"));

for (const alg of ["RSA-SHA1", "RSA-SHA256"]) {
  console.log(alg, createSign(alg).update("<SignedInfo>abc</SignedInfo>", "utf8").sign(PRIV1, "base64"));
}
console.log("pkcs8 same:", createSign("RSA-SHA1").update("x").sign(PRIV8, "base64") === createSign("RSA-SHA1").update("x").sign(PRIV1, "base64"));
console.log(createHash("sha1").update("abc", "utf8").digest("base64"));

const e1 = publicEncrypt({ key: PUB, padding: constants.RSA_PKCS1_PADDING }, key);
const e2 = publicEncrypt(PUB, key);
console.log("encrypted lengths:", e1.length, e2.length, e1.toString("base64") !== publicEncrypt({ key: PUB, padding: constants.RSA_PKCS1_PADDING }, key).toString("base64"));
