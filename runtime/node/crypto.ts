// xisto:crypto (node:crypto): randomUUID, createHash and createHmac over md5, sha1, sha256 and sha512,
// timingSafeEqual, randomInt/randomBytes, createCipheriv (aes-128/192/256-cbc), publicEncrypt (RSA PKCS#1 v1.5 and
// OAEP) and createSign (RSA PKCS#1 v1.5 with SHA-1/SHA-256/SHA-512). Hash data is text (UTF-8, Node's default for
// update(string)); a digest is text in an encoding (hex, base64, base64url, latin1): digest() with no encoding,
// which gives a Buffer in Node, is not here. RSA keys are PEM (SPKI or PKCS#1 public, PKCS#8 or PKCS#1 private).

declare function crypto_uuid(): string;
declare function crypto_supported(algorithm: string): boolean;
declare function crypto_digest(algorithm: string, data: string, encoding: string): string;
declare function crypto_hmac(algorithm: string, key: string, data: string, encoding: string): string;
declare function crypto_digest_bytes(algorithm: string, data: Uint8Array): Uint8Array;
declare function crypto_random_bytes(n: int): Uint8Array;

/** A random version 4 UUID, as 36 lowercase characters (8-4-4-4-12). */
export function randomUUID(): string {
  return crypto_uuid();
}

/** The digest algorithms createHash and createHmac take. */
export function getHashes(): string[] {
  return ["md5", "sha1", "sha256", "sha512"];
}

/** A hash being fed: update() as many times as needed, then digest() once. */
export class Hash {
  private algorithm: string;
  private parts: string[] = [];
  private finished: boolean = false;
  constructor(algorithm: string) {
    this.algorithm = algorithm;
  }
  /** Adds data (UTF-8 text; the encoding is accepted) to what is hashed. */
  update(data: string, inputEncoding: string = "utf8"): Hash {
    if (this.finished) throw new Error("Digest already called");
    this.parts.push(data);
    return this;
  }
  /** The digest of everything given to update(), in encoding (hex, base64, base64url or latin1). */
  digest(encoding: string): string {
    if (this.finished) throw new Error("Digest already called");
    this.finished = true;
    return crypto_digest(this.algorithm, this.parts.join(""), encoding);
  }
}

/** An HMAC being fed: update() as many times as needed, then digest() once. */
export class Hmac {
  private algorithm: string;
  private key: string;
  private parts: string[] = [];
  private finished: boolean = false;
  constructor(algorithm: string, key: string) {
    this.algorithm = algorithm;
    this.key = key;
  }
  /** Adds data (UTF-8 text; the encoding is accepted) to what is authenticated. */
  update(data: string, inputEncoding: string = "utf8"): Hmac {
    if (this.finished) throw new Error("Digest already called");
    this.parts.push(data);
    return this;
  }
  /** The HMAC of everything given to update(), in encoding (hex, base64, base64url or latin1). */
  digest(encoding: string): string {
    if (this.finished) throw new Error("Digest already called");
    this.finished = true;
    return crypto_hmac(this.algorithm, this.key, this.parts.join(""), encoding);
  }
}

/** A hash of the given algorithm (md5, sha1, sha256, sha512). Throws for any other. */
export function createHash(algorithm: string): Hash {
  if (!crypto_supported(algorithm)) throw new Error("Digest method not supported");
  return new Hash(algorithm);
}

/** An HMAC of the given algorithm with key (UTF-8 text). Throws for an algorithm other than the four. */
export function createHmac(algorithm: string, key: string): Hmac {
  if (!crypto_supported(algorithm)) throw new Error("Invalid digest: " + algorithm);
  return new Hmac(algorithm, key);
}

// ---- comparison and randomness ----

/** Compares two buffers in constant time; their lengths must be equal (RangeError otherwise, as Node). */
export function timingSafeEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) throw new RangeError("Input buffers must have the same byte length");
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff = diff | (a[i] ^ b[i]);
  return diff === 0;
}

/** n random bytes. */
export function randomBytes(n: int): Uint8Array {
  return crypto_random_bytes(n);
}

/** A random integer in [min, max) (randomInt(max) is [0, max)), without modulo bias. */
export function randomInt(min: int, max: int = -1): int {
  let lo = min;
  let hi = max;
  if (max < 0 && min >= 0) {
    lo = 0;
    hi = min;
  }
  if (hi <= lo) throw new RangeError(`The value of "max" is out of range. It must be greater than the value of "min" (${lo}). Received ${hi}`);
  const range = hi - lo;
  const limit = 281474976710656 - (281474976710656 % range); // 2^48, as Node's
  for (;;) {
    const b = crypto_random_bytes(6);
    let x = 0;
    for (let i = 0; i < 6; i++) x = x * 256 + b[i];
    if (x < limit) return lo + Math.trunc(x % range);
  }
}

/** node:crypto's constants: the RSA paddings. */
export class CryptoConstants {
  readonly RSA_NO_PADDING: int = 3;
  readonly RSA_PKCS1_PADDING: int = 1;
  readonly RSA_PKCS1_OAEP_PADDING: int = 4;
  readonly RSA_PKCS1_PSS_PADDING: int = 6;
}
export const constants: CryptoConstants = new CryptoConstants();

// ---- AES (FIPS 197), CBC with PKCS#7 padding ----

function aesSbox(): Uint8Array {
  const sbox = new Uint8Array(256);
  let p = 1;
  let q = 1;
  do {
    p = p ^ ((p << 1) & 0xff) ^ ((p & 0x80) !== 0 ? 0x1b : 0);
    q = q ^ (q << 1);
    q = q ^ (q << 2);
    q = q ^ (q << 4);
    q = q & 0xff;
    if ((q & 0x80) !== 0) q = q ^ 0x09;
    const x = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4));
    sbox[p] = (x ^ 0x63) & 0xff;
  } while (p !== 1);
  sbox[0] = 0x63;
  return sbox;
}

function xtime(b: int): int {
  return ((b << 1) ^ ((b & 0x80) !== 0 ? 0x1b : 0)) & 0xff;
}

// the round keys of a 16, 24 or 32 byte key
function aesExpand(key: Uint8Array, sbox: Uint8Array): Uint8Array {
  const nk = Math.trunc(key.length / 4);
  const rounds = nk + 6;
  const w = new Uint8Array(16 * (rounds + 1));
  for (let i = 0; i < key.length; i++) w[i] = key[i];
  let rcon = 1;
  for (let i = nk; i < 4 * (rounds + 1); i++) {
    let t0 = w[4 * (i - 1)];
    let t1 = w[4 * (i - 1) + 1];
    let t2 = w[4 * (i - 1) + 2];
    let t3 = w[4 * (i - 1) + 3];
    if (i % nk === 0) {
      const r0 = sbox[t1] ^ rcon;
      const r1 = sbox[t2];
      const r2 = sbox[t3];
      const r3 = sbox[t0];
      t0 = r0;
      t1 = r1;
      t2 = r2;
      t3 = r3;
      rcon = xtime(rcon);
    } else if (nk > 6 && i % nk === 4) {
      t0 = sbox[t0];
      t1 = sbox[t1];
      t2 = sbox[t2];
      t3 = sbox[t3];
    }
    w[4 * i] = w[4 * (i - nk)] ^ t0;
    w[4 * i + 1] = w[4 * (i - nk) + 1] ^ t1;
    w[4 * i + 2] = w[4 * (i - nk) + 2] ^ t2;
    w[4 * i + 3] = w[4 * (i - nk) + 3] ^ t3;
  }
  return w;
}

function aesBlock(s: Uint8Array, w: Uint8Array, sbox: Uint8Array): void {
  const rounds = Math.trunc(w.length / 16) - 1;
  const t = new Uint8Array(16);
  for (let i = 0; i < 16; i++) s[i] = s[i] ^ w[i];
  for (let r = 1; r <= rounds; r++) {
    // SubBytes and ShiftRows (column-major state)
    for (let c = 0; c < 4; c++) {
      for (let row = 0; row < 4; row++) t[4 * c + row] = sbox[s[4 * ((c + row) % 4) + row]];
    }
    if (r < rounds) {
      for (let c = 0; c < 4; c++) {
        const a0 = t[4 * c];
        const a1 = t[4 * c + 1];
        const a2 = t[4 * c + 2];
        const a3 = t[4 * c + 3];
        const all = a0 ^ a1 ^ a2 ^ a3;
        s[4 * c] = a0 ^ all ^ xtime(a0 ^ a1);
        s[4 * c + 1] = a1 ^ all ^ xtime(a1 ^ a2);
        s[4 * c + 2] = a2 ^ all ^ xtime(a2 ^ a3);
        s[4 * c + 3] = a3 ^ all ^ xtime(a3 ^ a0);
      }
    } else {
      for (let i = 0; i < 16; i++) s[i] = t[i];
    }
    for (let i = 0; i < 16; i++) s[i] = s[i] ^ w[16 * r + i];
  }
}

function concatBytes(parts: Uint8Array[]): Uint8Array {
  let n: int = 0;
  for (const p of parts) n += p.length;
  const out = new Uint8Array(n);
  let k = 0;
  for (const p of parts) {
    for (let i = 0; i < p.length; i++) out[k + i] = p[i];
    k += p.length;
  }
  return out;
}

/** A cipher being fed: update() gives the blocks done so far, final() the last one with its PKCS#7 padding. */
export class Cipher {
  private readonly w: Uint8Array;
  private readonly sbox: Uint8Array;
  private readonly prev: Uint8Array;
  private pending: Uint8Array = new Uint8Array(0);
  private finished: boolean = false;
  private autoPadding: boolean = true;
  constructor(key: Uint8Array, iv: Uint8Array) {
    this.sbox = aesSbox();
    this.w = aesExpand(key, this.sbox);
    this.prev = new Uint8Array(16);
    for (let i = 0; i < 16; i++) this.prev[i] = iv[i];
  }
  private blocks(data: Uint8Array, upto: int): Uint8Array {
    const out = new Uint8Array(upto);
    const s = new Uint8Array(16);
    for (let off = 0; off < upto; off += 16) {
      for (let i = 0; i < 16; i++) s[i] = data[off + i] ^ this.prev[i];
      aesBlock(s, this.w, this.sbox);
      for (let i = 0; i < 16; i++) {
        out[off + i] = s[i];
        this.prev[i] = s[i];
      }
    }
    return out;
  }
  /** Encrypts more data (text in inputEncoding, utf8 by default, or bytes); returns the whole blocks ready. */
  update(data: string | Uint8Array, inputEncoding: string = "utf8"): Buffer {
    if (this.finished) throw new Error("Unsupported state");
    const bytes: Uint8Array = typeof data === "string" ? Buffer.from(data as string, inputEncoding) : (data as Uint8Array);
    const all = concatBytes([this.pending, bytes]);
    const whole = all.length - (all.length % 16);
    const out = this.blocks(all, whole);
    this.pending = all.subarray(whole, all.length);
    return Buffer.from(out);
  }
  /** The last block, padded (PKCS#7). */
  final(): Buffer {
    if (this.finished) throw new Error("Unsupported state");
    this.finished = true;
    if (!this.autoPadding) {
      if (this.pending.length !== 0) throw new Error("wrong final block length");
      return Buffer.alloc(0);
    }
    const pad = 16 - this.pending.length;
    const last = new Uint8Array(16);
    for (let i = 0; i < this.pending.length; i++) last[i] = this.pending[i];
    for (let i = this.pending.length; i < 16; i++) last[i] = pad;
    return Buffer.from(this.blocks(last, 16));
  }
  /** setAutoPadding(false): the data must fill whole blocks. */
  setAutoPadding(on: boolean = true): Cipher {
    this.autoPadding = on;
    return this;
  }
}

/** A cipher: aes-128-cbc, aes-192-cbc or aes-256-cbc, with a key of 16, 24 or 32 bytes and a 16-byte IV. */
export function createCipheriv(algorithm: string, key: Uint8Array, iv: Uint8Array): Cipher {
  const alg = algorithm.toLowerCase();
  const bits = alg === "aes-128-cbc" ? 16 : alg === "aes-192-cbc" ? 24 : alg === "aes-256-cbc" ? 32 : 0;
  if (bits === 0) throw new Error("Invalid cipher: " + algorithm + " (xisto: aes-128/192/256-cbc)");
  if (key.length !== bits) throw new RangeError("Invalid key length");
  if (iv.length !== 16) throw new TypeError("Invalid initialization vector");
  return new Cipher(key, iv);
}

// ---- RSA (PKCS#1 v2.2) over bigint ----

function toBig(b: Uint8Array): bigint {
  let n = 0n;
  for (let i = 0; i < b.length; i++) n = n * 256n + BigInt(b[i]);
  return n;
}

function fromBig(n: bigint, len: int): Uint8Array {
  const out = new Uint8Array(len);
  let x = n;
  for (let i = len - 1; i >= 0; i--) {
    out[i] = Math.trunc(Number(x % 256n));
    x = x / 256n;
  }
  return out;
}

function modPow(base: bigint, exp: bigint, m: bigint): bigint {
  let r = 1n;
  let x = base % m;
  let e = exp;
  while (e > 0n) {
    if (e % 2n === 1n) r = (r * x) % m;
    x = (x * x) % m;
    e = e / 2n;
  }
  return r;
}

class Der {
  b: Uint8Array;
  at: int = 0;
  constructor(b: Uint8Array) {
    this.b = b;
  }
  // a TLV's tag and its body's bounds
  next(tag: int): Uint8Array {
    if (this.at >= this.b.length || this.b[this.at] !== tag) throw new Error("error:1E08010C:DECODER routines::unsupported");
    this.at++;
    let len = this.b[this.at];
    this.at++;
    if (len >= 0x80) {
      const n = len - 0x80;
      len = 0;
      for (let i = 0; i < n; i++) {
        len = len * 256 + this.b[this.at];
        this.at++;
      }
    }
    const body = this.b.subarray(this.at, this.at + len);
    this.at += len;
    return body;
  }
  int(): bigint {
    return toBig(this.next(0x02));
  }
}

class RsaKey {
  n: bigint = 0n;
  e: bigint = 0n;
  d: bigint = 0n;
  p: bigint = 0n;
  q: bigint = 0n;
  dp: bigint = 0n;
  dq: bigint = 0n;
  qi: bigint = 0n;
  size: int = 0; // the modulus in bytes
}

function pemBody(pem: string): Uint8Array {
  const lines = pem.trim().split("\n");
  let b64 = "";
  for (const l of lines) {
    const t = l.trim();
    if (t.length === 0 || t.startsWith("-----")) continue;
    b64 += t;
  }
  return Buffer.from(b64, "base64");
}

function modulusSize(n: bigint): int {
  let len = 0;
  let x = n;
  while (x > 0n) {
    x = x / 256n;
    len++;
  }
  return len;
}

function publicKeyOf(pem: string): RsaKey {
  const k = new RsaKey();
  const der = new Der(pemBody(pem));
  let seq = new Der(der.next(0x30));
  if (pem.includes("BEGIN PUBLIC KEY")) { // SPKI: { algorithm, BIT STRING { RSAPublicKey } }
    seq.next(0x30);
    const bits = seq.next(0x03);
    seq = new Der(new Der(bits.subarray(1, bits.length)).next(0x30));
  } else if (pem.includes("PRIVATE KEY")) {
    return privateKeyOf(pem);
  }
  k.n = seq.int();
  k.e = seq.int();
  k.size = modulusSize(k.n);
  return k;
}

function privateKeyOf(pem: string): RsaKey {
  const k = new RsaKey();
  let seq = new Der(new Der(pemBody(pem)).next(0x30));
  if (pem.includes("BEGIN PRIVATE KEY")) { // PKCS#8: { version, algorithm, OCTET STRING { RSAPrivateKey } }
    seq.int();
    seq.next(0x30);
    seq = new Der(new Der(seq.next(0x04)).next(0x30));
  }
  seq.int(); // version
  k.n = seq.int();
  k.e = seq.int();
  k.d = seq.int();
  k.p = seq.int();
  k.q = seq.int();
  k.dp = seq.int();
  k.dq = seq.int();
  k.qi = seq.int();
  k.size = modulusSize(k.n);
  return k;
}

// m^d mod n by the CRT
function rsaPrivate(k: RsaKey, m: bigint): bigint {
  const m1 = modPow(m, k.dp, k.p);
  const m2 = modPow(m, k.dq, k.q);
  let h = ((m1 - m2) * k.qi) % k.p;
  if (h < 0n) h = h + k.p;
  return m2 + h * k.q;
}

function mgf1(hash: string, seed: Uint8Array, len: int): Uint8Array {
  const out = new Uint8Array(len);
  let k = 0;
  let counter = 0;
  while (k < len) {
    const c = new Uint8Array(4);
    c[0] = (counter >> 24) & 0xff;
    c[1] = (counter >> 16) & 0xff;
    c[2] = (counter >> 8) & 0xff;
    c[3] = counter & 0xff;
    const d = crypto_digest_bytes(hash, concatBytes([seed, c]));
    for (let i = 0; i < d.length && k < len; i++) {
      out[k] = d[i];
      k++;
    }
    counter++;
  }
  return out;
}

/** What publicEncrypt takes: the key (PEM) and the padding (constants.RSA_PKCS1_OAEP_PADDING by default). */
export class PublicKeyInput {
  key: string;
  padding?: int;
  oaepHash?: string;
}

/**
 * Encrypts a short buffer with an RSA public key: PKCS#1 v1.5 (constants.RSA_PKCS1_PADDING) or OAEP (the default,
 * SHA-1 unless oaepHash says otherwise).
 */
export function publicEncrypt(key: PublicKeyInput | string, data: Uint8Array): Buffer {
  const input: PublicKeyInput = typeof key === "string" ? { key: key as string } : (key as PublicKeyInput);
  const k = publicKeyOf(input.key);
  const padding = input.padding ?? 4;
  const em = new Uint8Array(k.size);
  if (padding === 1) {
    if (data.length > k.size - 11) throw new Error("error:0200006E:rsa routines::data too large for key size");
    em[1] = 2;
    const ps = k.size - 3 - data.length;
    let i = 0;
    while (i < ps) { // nonzero random bytes
      const r = crypto_random_bytes(ps - i);
      for (let j = 0; j < r.length && i < ps; j++) {
        if (r[j] !== 0) {
          em[2 + i] = r[j];
          i++;
        }
      }
    }
    for (let j = 0; j < data.length; j++) em[3 + ps + j] = data[j];
  } else if (padding === 4) {
    const hash = (input.oaepHash ?? "sha1").toLowerCase();
    const hl = crypto_digest_bytes(hash, new Uint8Array(0)).length;
    if (data.length > k.size - 2 * hl - 2) throw new Error("error:0200006E:rsa routines::data too large for key size");
    const db = new Uint8Array(k.size - hl - 1);
    const lhash = crypto_digest_bytes(hash, new Uint8Array(0));
    for (let j = 0; j < hl; j++) db[j] = lhash[j];
    db[db.length - data.length - 1] = 1;
    for (let j = 0; j < data.length; j++) db[db.length - data.length + j] = data[j];
    const seed = crypto_random_bytes(hl);
    const dbMask = mgf1(hash, seed, db.length);
    for (let j = 0; j < db.length; j++) db[j] = db[j] ^ dbMask[j];
    const seedMask = mgf1(hash, db, hl);
    for (let j = 0; j < hl; j++) em[1 + j] = seed[j] ^ seedMask[j];
    for (let j = 0; j < db.length; j++) em[1 + hl + j] = db[j];
  } else {
    throw new Error("xisto: publicEncrypt supports RSA_PKCS1_PADDING and RSA_PKCS1_OAEP_PADDING");
  }
  return Buffer.from(fromBig(modPow(toBig(em), k.e, k.n), k.size));
}

// DigestInfo's prefix per hash (RFC 8017, 9.2 note 1)
function digestInfo(hash: string): string {
  if (hash === "sha1") return "3021300906052b0e03021a05000414";
  if (hash === "sha256") return "3031300d060960864801650304020105000420";
  if (hash === "sha512") return "3051300d060960864801650304020305000440";
  throw new Error("Invalid digest: " + hash);
}

/** A signature being fed: update() the data, then sign(privateKeyPem, encoding). RSA PKCS#1 v1.5. */
export class Sign {
  private readonly hash: string;
  private parts: string[] = [];
  constructor(hash: string) {
    this.hash = hash;
  }
  /** Adds data (UTF-8 text). */
  update(data: string, inputEncoding: string = "utf8"): Sign {
    this.parts.push(data);
    return this;
  }
  /** The signature with the private key (PEM, PKCS#1 or PKCS#8), as text in outputEncoding (base64, hex...). */
  sign(privateKey: string, outputEncoding: string): string {
    const k = privateKeyOf(privateKey);
    const h = Buffer.from(crypto_digest(this.hash, this.parts.join(""), "hex"), "hex");
    const t = concatBytes([Buffer.from(digestInfo(this.hash), "hex"), h]);
    if (t.length > k.size - 11) throw new Error("error:0200006E:rsa routines::digest too big for rsa key");
    const em = new Uint8Array(k.size);
    em[1] = 1;
    for (let i = 2; i < k.size - t.length - 1; i++) em[i] = 0xff;
    for (let i = 0; i < t.length; i++) em[k.size - t.length + i] = t[i];
    const sig = fromBig(rsaPrivate(k, toBig(em)), k.size);
    return Buffer.from(sig).toString(outputEncoding);
  }
}

/** RSA-SHA1, RSA-SHA256, RSA-SHA512 (or sha1/sha256/sha512). */
export function createSign(algorithm: string): Sign {
  let a = algorithm.toLowerCase();
  if (a.startsWith("rsa-")) a = a.slice(4);
  if (a !== "sha1" && a !== "sha256" && a !== "sha512") throw new Error("Invalid digest");
  return new Sign(a);
}
