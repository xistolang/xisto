// node:crypto keys and signatures: PEM import and export (spki, pkcs1, pkcs8, sec1) round trips, DER and JWK,
// createPublicKey from a private key, RSA PKCS#1 v1.5 signatures (deterministic: Node's bytes), RSA-PSS and ECDSA
// (random: checked by verifying, ours with Node's keys and Node's signatures with ours), generateKeyPairSync,
// createSecretKey. The keys are throwaway test keys made for this file.
import * as crypto from "node:crypto";
import { createPrivateKey, createPublicKey, createSign, createVerify, generateKeyPairSync, KeyObject, sign, verify } from "node:crypto";

const RSAPRIV1: string = "-----BEGIN RSA PRIVATE KEY-----\nMIICWwIBAAKBgQCwD/XocMD/pUv2gm8S9BgMAnzrIevhaIyccSTRW0rttGTwa2CG\ntPgqykUxyroLMdEGk6UmIcsaND2ISeX5vJJLII2k+LaZ4g1GpozFUzg/MnsY91pi\nRADYBvfig3GSWDyJnhYG1rPYEYCETsj5IQAvPuOirogsiU5pQEf7OJ9AeQIDAQAB\nAoGAYJbA/94r45RZkkmIiktjjeMniQ+5CpaC3ais3DZLaxEWd1dRQ1Ww+DizsVdY\nrZ+M3yDM75iGhj+2qjhwdIvhbXAZ5ubz/NXM0vIUcE3eLkgnd44AwNkta+ZW7F8O\nNbjv5MuFXElvzFK4MkC5YSjrU/QIQy1jVx1AfKx1jUDkZakCQQDmd55M3hdN2EU+\nbMar1R/vRTACQ81WVF8CjU5FWoOtl4xllt1f8UNwEpQnC/wk3pgAhkZv0NfCUIvO\ngdXdP8kPAkEAw5FXR7qxMHg3B5uZTXhA+0FrFxBLsMBUl4rfDDAAP0KeRQ5ARXkv\n6y1HmV16gxWNKnvIkwesCdMk8FdbhjmN9wJAHAb8B1kS+HKOqTLNTmRSx+RgHEZm\nmFMWYtcQZ7rEPL4rYsdgEKTkkKKRxdXdShaP4bByrHJ3argk6G49tfRl3QJAdJdO\ngH3lqu7QZ+G0yIG7w88haBOX/Fz9UYdCKILpcs1li+fOG1z/W2aYO++F3WZYzY4U\nast1HjpZ2EtMTj47ywJAMZ9n/C2KLDNcDwmJQqERY1Dc/NHKWdukDI0XXvUAvxvv\nQRUb3TVBLXORs5BW9hRWhC3DQXts1A2UhG1Xqw0vQg==\n-----END RSA PRIVATE KEY-----\n";
const RSAPUB: string = "-----BEGIN PUBLIC KEY-----\nMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCwD/XocMD/pUv2gm8S9BgMAnzr\nIevhaIyccSTRW0rttGTwa2CGtPgqykUxyroLMdEGk6UmIcsaND2ISeX5vJJLII2k\n+LaZ4g1GpozFUzg/MnsY91piRADYBvfig3GSWDyJnhYG1rPYEYCETsj5IQAvPuOi\nrogsiU5pQEf7OJ9AeQIDAQAB\n-----END PUBLIC KEY-----\n";
const ECPRIV8: string = "-----BEGIN PRIVATE KEY-----\nMIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg6qgZJ91WDg2SgkCb\nzMiyHi+GTW+gNeO9vnosn8+SpMmhRANCAAToUbSQCwCOjj1ejnGqlsrRASYZR5Vg\n9vVMkqs+7nGsPEPLlE/NJi9U3uYyQWxvCEZu/6nDq/Ioc0EtQvG9Z7uf\n-----END PRIVATE KEY-----\n";
const ECPUB: string = "-----BEGIN PUBLIC KEY-----\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE6FG0kAsAjo49Xo5xqpbK0QEmGUeV\nYPb1TJKrPu5xrDxDy5RPzSYvVN7mMkFsbwhGbv+pw6vyKHNBLULxvWe7nw==\n-----END PUBLIC KEY-----\n";
const EC384SEC1: string = "-----BEGIN EC PRIVATE KEY-----\nMIGkAgEBBDCaUWqI2RBq+3/dR14XvZoDY+4P2U5k/UFjqB9t3Y4LS2C172rk2OMw\niefGveJJOY2gBwYFK4EEACKhZANiAAT1QhERheRo89mHyrD9Q0U7nB+zJ76owfL1\nRXgARgwZFhrn7Hc228xAAUUGvPLQpHrnlWQFGVJgQA1Gqz69xQHBYTRSVfiVg0tB\nK3TzD55Qmfe+FSLhhUyl7eB1FuLJIqE=\n-----END EC PRIVATE KEY-----\n";
const ECDSASIG: string = "MEQCID390sJxUyO9zrq2rLHm+YaRopmDZXnSmFBPEFBRiu06AiAPUU2ZeDpScqF6xs8ZvtgtcH7P2zDui/Ak8tEZLdDtTw==";
const ECDSAP1363: string = "n2XhtpzrD3AXP8dSE4XI+yQ2YS5fEIrZh/0buiFpjs7/R4F3yxTIhIXqTL97bSg3m6XxfCPigOiEHyEOg+9T+A==";
const PSSSIG: string = "eSoVd7VShlBTY57ZkO2OOjslOMbbAhIwet09ndbWzhXdfyPK9LDvjwOJbSadBwDZBuMXjtd2dcQU0fbhJdBZgzaDXo5JN0csJD8UP34Kh694YfOp+bFgu8/Ys/PIED3Xc/48JzRPwyzRhof84JWRo7WQTB+K8tW178xWAMBGsiE=";
const EC384SIG: string = "MGUCMQDKqOnldfo3omZrCvq/xrNRJcrzF7ryUZrnkOeGsAWNr81avPbwg54nwY/gnzyUHrECMECINNmwxeeAiuWDADVzScDWRZItm2wQ2cEJDDQ2MW4ZECxDPCA5VigW6spU/wv0yw==";

// import and export: the same text back
const rsaPriv = createPrivateKey(RSAPRIV1);
const rsaPub = createPublicKey(RSAPUB);
const ecPriv = createPrivateKey(ECPRIV8);
const ecPub = createPublicKey(ECPUB);
const ec384 = createPrivateKey(EC384SEC1);
console.log(rsaPriv.type, rsaPriv.asymmetricKeyType, rsaPub.type, ecPriv.asymmetricKeyType, ec384.asymmetricKeyType);
console.log(rsaPriv.export({ type: "pkcs1", format: "pem" }) === RSAPRIV1);
console.log(rsaPub.export({ type: "spki", format: "pem" }) === RSAPUB);
console.log(ecPriv.export({ type: "pkcs8", format: "pem" }) === ECPRIV8);
console.log(ecPub.export({ type: "spki", format: "pem" }) === ECPUB);
console.log(ec384.export({ type: "sec1", format: "pem" }) === EC384SEC1);
console.log(rsaPriv.export({ type: "pkcs8", format: "pem" }));
console.log(rsaPub.export({ type: "pkcs1", format: "pem" }));
console.log(ecPriv.export({ type: "sec1", format: "pem" }));
console.log(ec384.export({ type: "pkcs8", format: "pem" }));
console.log(createPublicKey(RSAPRIV1).export({ type: "spki", format: "pem" }) === RSAPUB);
console.log(createPublicKey(ecPriv).export({ type: "spki", format: "pem" }) === ECPUB);
console.log(rsaPub.export({ type: "spki", format: "der" }).toString("hex"));
console.log(ecPriv.export({ type: "pkcs8", format: "der" }).toString("base64"));
const jwk = rsaPriv.export({ format: "jwk" });
console.log(jwk.kty, jwk.n, jwk.e, jwk.d, jwk.qi);
const ejwk = ecPriv.export({ format: "jwk" });
console.log(ejwk.kty, ejwk.crv, ejwk.x, ejwk.y, ejwk.d);
const pjwk = ecPub.export({ format: "jwk" });
console.log(pjwk.kty, pjwk.crv, pjwk.x, pjwk.y, pjwk.d ?? "no d");
console.log(createPublicKey({ key: pjwk, format: "jwk" }).export({ type: "spki", format: "pem" }) === ECPUB);
console.log(createPrivateKey({ key: jwk, format: "jwk" }).export({ type: "pkcs1", format: "pem" }) === RSAPRIV1);
const der = rsaPub.export({ type: "spki", format: "der" });
console.log(createPublicKey({ key: der, format: "der", type: "spki" }).equals(rsaPub), rsaPub.equals(ecPub));
console.log(rsaPriv.asymmetricKeyDetails!.modulusLength, rsaPriv.asymmetricKeyDetails!.publicExponent, ecPub.asymmetricKeyDetails!.namedCurve, ec384.asymmetricKeyDetails!.namedCurve);

// RSA PKCS#1 v1.5: the same bytes as Node
const data = Buffer.from("the data to sign: ação");
for (const alg of ["sha1", "sha224", "sha256", "sha384", "sha512", "RSA-SHA256"]) {
  const s = createSign(alg).update(data).sign(rsaPriv, "base64");
  console.log(alg, s, createVerify(alg).update(data).verify(rsaPub, s, "base64"), createVerify(alg).update("other").verify(rsaPub, s, "base64"));
}
const oneShot = sign("sha256", data, RSAPRIV1);
console.log(oneShot.toString("hex"), verify("sha256", data, RSAPUB, oneShot), verify(null, data, rsaPub, oneShot));
const s2 = createSign("SHA256");
s2.update("part one, ");
s2.update(Buffer.from("part two"));
console.log(s2.sign(RSAPRIV1).toString("base64url"));

// RSA-PSS and ECDSA: verified both ways
const pss = sign("sha256", data, { key: rsaPriv, padding: crypto.constants.RSA_PKCS1_PSS_PADDING });
console.log(pss.length, verify("sha256", data, { key: rsaPub, padding: crypto.constants.RSA_PKCS1_PSS_PADDING }, pss));
const pss32 = sign("sha256", data, { key: rsaPriv, padding: crypto.constants.RSA_PKCS1_PSS_PADDING, saltLength: 32 });
console.log(verify("sha256", data, { key: rsaPub, padding: crypto.constants.RSA_PKCS1_PSS_PADDING, saltLength: 32 }, pss32));
const signedByNode = Buffer.from("signed by node");
console.log(verify("sha256", signedByNode, { key: RSAPUB, padding: crypto.constants.RSA_PKCS1_PSS_PADDING }, Buffer.from(PSSSIG, "base64")));
console.log(verify("sha256", signedByNode, ECPUB, Buffer.from(ECDSASIG, "base64")));
console.log(verify("sha256", signedByNode, { key: ecPub, dsaEncoding: "ieee-p1363" }, Buffer.from(ECDSAP1363, "base64")));
console.log(verify("sha384", signedByNode, ec384, Buffer.from(EC384SIG, "base64")));
console.log(verify("sha256", Buffer.from("tampered"), ECPUB, Buffer.from(ECDSASIG, "base64")));
const es = createSign("sha256").update(data).sign(ecPriv);
console.log(es[0], createVerify("sha256").update(data).verify(ecPub, es), createVerify("sha256").update(data).verify(ECPRIV8, es));
const raw = sign("sha256", data, { key: ecPriv, dsaEncoding: "ieee-p1363" });
console.log(raw.length, verify("sha256", data, { key: ecPub, dsaEncoding: "ieee-p1363" }, raw));
const es384 = sign("sha512", data, ec384);
console.log(verify("sha512", data, ec384, es384), verify("sha256", data, ec384, es384));

// new key pairs
const rsaPair = generateKeyPairSync("rsa", { modulusLength: 1024 });
console.log(rsaPair.publicKey.type, rsaPair.privateKey.type, rsaPair.privateKey.asymmetricKeyDetails!.modulusLength, rsaPair.publicKey.asymmetricKeyDetails!.publicExponent);
const rs = sign("sha256", data, rsaPair.privateKey);
console.log(rs.length, verify("sha256", data, rsaPair.publicKey, rs));
const again = createPrivateKey(rsaPair.privateKey.export({ type: "pkcs8", format: "pem" }));
console.log(again.equals(rsaPair.privateKey), sign("sha256", data, again).equals(rs));
const pem = generateKeyPairSync("ec", {
  namedCurve: "P-256",
  publicKeyEncoding: { type: "spki", format: "pem" },
  privateKeyEncoding: { type: "pkcs8", format: "pem" }
});
console.log(pem.publicKey.split("\n")[0], pem.privateKey.split("\n")[0], pem.publicKey.length, pem.privateKey.length);
const ps = createSign("sha256").update("pem pair").sign(pem.privateKey, "hex");
console.log(createVerify("sha256").update("pem pair").verify(pem.publicKey, ps, "hex"));
const p384 = generateKeyPairSync("ec", { namedCurve: "secp384r1" });
console.log(p384.privateKey.asymmetricKeyDetails!.namedCurve, verify("sha384", data, p384.publicKey, sign("sha384", data, p384.privateKey)));
const rsaPem = generateKeyPairSync("rsa", {
  modulusLength: 1024,
  publicKeyEncoding: { type: "pkcs1", format: "pem" },
  privateKeyEncoding: { type: "pkcs1", format: "pem" }
});
console.log(rsaPem.publicKey.split("\n")[0], rsaPem.privateKey.split("\n")[0]);
console.log(verify("sha256", data, rsaPem.publicKey, sign("sha256", data, rsaPem.privateKey)));

// secret keys
const secret = crypto.createSecretKey(Buffer.from("0123456789abcdef"));
console.log(secret.type, secret.symmetricKeySize, secret.asymmetricKeyType == null, secret.export().toString("hex"));
console.log(crypto.createHmac("sha256", secret).update("x").digest("hex") === crypto.createHmac("sha256", "0123456789abcdef").update("x").digest("hex"));
console.log(JSON.stringify(secret.export({ format: "jwk" })));

// errors
function tryIt(label: string, f: () => void): void {
  try {
    f();
    console.log(label, "ok");
  } catch (e: any) {
    console.log(label, e.name, e.code ?? "-", e.message);
  }
}
tryIt("garbage pem", () => createPublicKey("hello"));
tryIt("bad body", () => createPrivateKey("-----BEGIN PRIVATE KEY-----\nAAAA\n-----END PRIVATE KEY-----"));
tryIt("bad digest", () => createSign("foo"));
tryIt("bad curve", () => generateKeyPairSync("ec", { namedCurve: "foo" }));
tryIt("bad type", () => generateKeyPairSync("dsa2", { modulusLength: 1024 }));
tryIt("verify bad sig", () => console.log(verify("sha256", data, ecPub, Buffer.alloc(10))));
