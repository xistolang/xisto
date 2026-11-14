// node:crypto: AES in cbc, ctr, gcm and ecb both ways (fixed keys and IVs: Node's bytes), text in and out in
// encodings across update() calls, GCM's AAD and tags, setAutoPadding, the errors; pbkdf2, scrypt and hkdf (sync and
// callback); sha224/sha384, hashing bytes, Hash.copy, getRandomValues.
import * as crypto from "node:crypto";
import { createCipheriv, createDecipheriv, hkdfSync, pbkdf2Sync, scryptSync } from "node:crypto";

const key16 = Buffer.from("000102030405060708090a0b0c0d0e0f", "hex");
const key24 = Buffer.alloc(24, 7);
const key32 = Buffer.from("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", "hex");
const iv = Buffer.from("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", "hex");
const texts = ["", "a", "exactly 16 bytes", "a text longer than one block, with ação and 日本語 in it", "x".repeat(1000)];

for (const bits of ["128", "192", "256"]) {
  const key = bits === "128" ? key16 : bits === "192" ? key24 : key32;
  for (const mode of ["cbc", "ctr", "ecb"]) {
    const alg = `aes-${bits}-${mode}`;
    const v = mode === "ecb" ? null : iv;
    for (const t of texts) {
      const c = createCipheriv(alg, key, v);
      const enc = Buffer.concat([c.update(t, "utf8"), c.final()]);
      const d = createDecipheriv(alg, key, v);
      const dec = Buffer.concat([d.update(enc), d.final()]).toString("utf8");
      console.log(alg, t.length, enc.length, crypto.createHash("sha256").update(enc).digest("hex").slice(0, 16), dec === t);
    }
  }
}

// text in and out with encodings, data fed in pieces that cut blocks and characters
const msg = "pieces: ação, 日本語, emoji 😀 and more text to cross blocks";
for (const alg of ["aes-256-cbc", "aes-256-ctr", "aes-256-gcm"]) {
  const v = alg.endsWith("gcm") ? iv.subarray(0, 12) : iv;
  const c = createCipheriv(alg, key32, v);
  let hex = c.update(msg.slice(0, 7), "utf8", "hex");
  hex += c.update(msg.slice(7, 30), "utf8", "hex");
  hex += c.update(msg.slice(30), "utf8", "hex");
  hex += c.final("hex");
  console.log(alg, hex);
  const tag = alg.endsWith("gcm") ? c.getAuthTag() : Buffer.alloc(0);
  const d = createDecipheriv(alg, key32, v);
  if (alg.endsWith("gcm")) d.setAuthTag(tag);
  let out = "";
  const encBytes = Buffer.from(hex, "hex");
  for (let i = 0; i < encBytes.length; i += 5) out += d.update(encBytes.subarray(i, Math.min(i + 5, encBytes.length)), null, "utf8");
  out += d.final("utf8");
  console.log(out === msg, out);
  const b64 = createCipheriv(alg, key32, v);
  console.log(b64.update(Buffer.from(msg), null, "base64") + b64.final("base64"));
}

// GCM: AAD, tags, other IV lengths and tag lengths
const ivLens: int[] = [12, 16, 8];
for (const ivLen of ivLens) {
  const giv = Buffer.alloc(ivLen, 3);
  const c = createCipheriv("aes-128-gcm", key16, giv);
  c.setAAD(Buffer.from("header"));
  const enc = Buffer.concat([c.update("secret message"), c.final()]);
  const tag = c.getAuthTag();
  console.log(ivLen, enc.toString("hex"), tag.toString("hex"));
  const d = createDecipheriv("aes-128-gcm", key16, giv);
  d.setAAD(Buffer.from("header"));
  d.setAuthTag(tag);
  console.log(Buffer.concat([d.update(enc), d.final()]).toString());
  const bad = createDecipheriv("aes-128-gcm", key16, giv);
  bad.setAAD(Buffer.from("other header"));
  bad.setAuthTag(tag);
  bad.update(enc);
  try {
    bad.final();
  } catch (e) {
    console.log("bad aad:", (e as Error).message);
  }
}
const short = createCipheriv("aes-256-gcm", key32, iv.subarray(0, 12), { authTagLength: 8 });
short.update("x");
short.final();
console.log(short.getAuthTag().toString("hex"));
const empty = createCipheriv("aes-256-gcm", key32, iv.subarray(0, 12));
empty.final();
console.log(empty.getAuthTag().toString("hex"));

// no padding
const np = createCipheriv("aes-128-cbc", key16, iv).setAutoPadding(false);
const npEnc = Buffer.concat([np.update(Buffer.alloc(32, 1)), np.final()]);
console.log(npEnc.toString("hex"));
const npd = createDecipheriv("aes-128-cbc", key16, iv);
npd.setAutoPadding(false);
console.log(Buffer.concat([npd.update(npEnc), npd.final()]).toString("hex"));

function tryIt(label: string, f: () => void): void {
  try {
    f();
    console.log(label, "ok");
  } catch (e: any) {
    console.log(label, e.name, e.code ?? "-", e.message);
  }
}
tryIt("key length", () => createDecipheriv("aes-256-cbc", Buffer.alloc(5), iv));
tryIt("iv length", () => createDecipheriv("aes-256-cbc", key32, Buffer.alloc(5)));
tryIt("unknown", () => createDecipheriv("aes-256-xyz", key32, iv));
tryIt("bad decrypt", () => {
  const d = createDecipheriv("aes-256-cbc", key32, iv);
  d.update(Buffer.alloc(16));
  d.final();
});
tryIt("final length", () => {
  const d = createDecipheriv("aes-256-cbc", key32, iv);
  d.update(Buffer.alloc(10));
  d.final();
});
tryIt("no padding length", () => {
  const c = createCipheriv("aes-128-cbc", key16, iv).setAutoPadding(false);
  c.update("abc");
  c.final();
});
tryIt("tag not set", () => {
  const d = createDecipheriv("aes-256-gcm", key32, iv.subarray(0, 12));
  d.update(Buffer.alloc(10));
  d.final();
});
tryIt("bad tag", () => {
  const d = createDecipheriv("aes-256-gcm", key32, iv.subarray(0, 12));
  d.setAuthTag(Buffer.alloc(16));
  d.update(Buffer.alloc(10));
  d.final();
});
tryIt("getAuthTag early", () => createCipheriv("aes-256-gcm", key32, iv.subarray(0, 12)).getAuthTag());
tryIt("update after final", () => {
  const c = createCipheriv("aes-128-cbc", key16, iv);
  c.final();
  c.update("x");
});
tryIt("final twice", () => {
  const c = createCipheriv("aes-128-cbc", key16, iv);
  c.final();
  c.final();
});

// key derivation
for (const digest of ["sha1", "sha256", "sha512", "sha384", "md5", "SHA256"]) {
  console.log("pbkdf2", digest, pbkdf2Sync("password", "salt", 1000, 40, digest).toString("hex"));
}
console.log(pbkdf2Sync(Buffer.from("pw"), Buffer.from([1, 2, 3]), 1, 100, "sha256").toString("base64"));
console.log(pbkdf2Sync("", "", 2, 1, "sha256").length);
console.log(scryptSync("password", "salt", 64).toString("hex"));
console.log(scryptSync("pleaseletmein", "SodiumChloride", 64, { N: 1024, r: 8, p: 16 }).toString("hex"));
console.log(scryptSync(Buffer.from("pw"), "s", 16, { cost: 256, blockSize: 4, parallelization: 2 }).toString("hex"));
console.log(Buffer.from(hkdfSync("sha256", "input key", "salt", "info", 42)).toString("hex"));
console.log(Buffer.from(hkdfSync("sha512", Buffer.alloc(22, 0x0b), Buffer.alloc(0), "", 100)).toString("base64"));
console.log(Buffer.from(hkdfSync("sha1", crypto.createSecretKey(Buffer.from("k")), "s", "i", 20)).toString("hex"));
tryIt("pbkdf2 digest", () => pbkdf2Sync("a", "b", 1, 10, "foo"));
tryIt("pbkdf2 iterations", () => pbkdf2Sync("a", "b", 0, 10, "sha256"));
tryIt("scrypt N", () => scryptSync("a", "b", 10, { N: 3 }));
tryIt("scrypt memory", () => scryptSync("a", "b", 10, { N: 1048576 }));
tryIt("hkdf length", () => hkdfSync("sha256", "k", "s", "i", 255 * 32 + 1));
tryIt("hkdf digest", () => hkdfSync("foo", "k", "s", "i", 10));

// hashes of bytes, more digests, copy
console.log(crypto.createHash("sha224").update("abc").digest("hex"));
console.log(crypto.createHash("sha384").update("abc").digest("base64"));
console.log(crypto.createHash("SHA-512").update(Buffer.from([0, 1, 2, 255])).digest("hex").slice(0, 32));
console.log(crypto.createHash("sha256").update("ab").update(Buffer.from("cd")).update("6566", "hex").digest("hex"));
console.log(crypto.createHmac("sha384", Buffer.from("key")).update(Buffer.from("data")).digest("base64url"));
console.log(crypto.createHmac("sha224", "key").update("data").digest().toString("hex"));
const h = crypto.createHash("sha256").update("shared prefix ");
const h2 = h.copy();
console.log(h.update("one").digest("hex"), h2.update("two").digest("hex"));
console.log(crypto.hash("sha1", "abc"), crypto.hash("md5", Buffer.from("abc"), "base64"));
console.log(crypto.getHashes().includes("sha384"), crypto.getCiphers().includes("aes-256-gcm"));
const rv = crypto.getRandomValues(new Uint8Array(32));
const rb = crypto.randomBytes(16);
console.log(rv.length, rb.length, rb.toString("hex").length);

// the callback forms
crypto.pbkdf2("password", "salt", 1, 32, "sha256", (err, key) => {
  console.log("pbkdf2 cb", err, key.toString("hex"));
  crypto.scrypt("password", "salt", 16, (err2, key2) => {
    console.log("scrypt cb", err2, key2.toString("hex"));
    crypto.scrypt("password", "salt", 16, { N: 1024 }, (err3, key3) => {
      console.log("scrypt cb options", err3, key3.toString("hex"));
      try {
        crypto.pbkdf2("a", "b", 1, 10, "nope", (err4, key4) => console.log("not called"));
      } catch (e) {
        console.log("pbkdf2 cb error", (e as Error).message);
      }
    });
  });
});
