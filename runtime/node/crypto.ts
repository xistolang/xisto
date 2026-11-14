// xisto:crypto (node:crypto). Hashes and HMACs (md5, sha1, sha224, sha256, sha384, sha512) of text or bytes;
// randomUUID, randomBytes, randomInt, getRandomValues, timingSafeEqual; AES ciphers (createCipheriv and
// createDecipheriv: aes-128/192/256 in cbc, ctr, gcm and ecb); pbkdf2, scrypt and hkdf; keys (createPublicKey,
// createPrivateKey, createSecretKey, KeyObject with PEM/DER/JWK export, generateKeyPairSync for rsa and ec);
// signatures (createSign/createVerify, sign/verify: RSA PKCS#1 v1.5 and PSS, ECDSA on P-256/P-384/P-521);
// publicEncrypt (RSA PKCS#1 v1.5 and OAEP) and a webcrypto.subtle subset (digest, HMAC keys, sign/verify).
// Hashes, HMACs and the KDFs are runtime/xisto_crypto.h's own C; ciphers, RSA and EC are BearSSL's
// (build/libbearssl.a, linked with this module; a program takes only the functions it calls).
// Results with an encoding are text and without one a Buffer, as in Node: the compiler sends m(args) to
// __m_bytes when the encoding is left out (checker.c, crypto_method_overload), and generateKeyPairSync with
// publicKeyEncoding/privateKeyEncoding to __generateKeyPairSync_pem (the keys as PEM text).
// Differences from Node: docs/docs/guides/node-compatibility.md.

declare function crypto_uuid(): string;
declare function crypto_digest(algorithm: string, data: string, encoding: string): string;
declare function crypto_hmac(algorithm: string, key: string, data: string, encoding: string): string;
declare function crypto_digest_bytes(algorithm: string, data: Uint8Array): Uint8Array;
declare function crypto_hmac_bytes(algorithm: string, key: Uint8Array, data: string): Uint8Array;
declare function crypto_random_bytes(n: int): Uint8Array;
declare function crypto_hash2(algorithm: string, data: Uint8Array): Buffer | null;
declare function crypto_hash2_text(algorithm: string, data: Uint8Array, encoding: string): string;
declare function crypto_hmac2_text(algorithm: string, key: Uint8Array, data: Uint8Array, encoding: string): string;
declare function crypto_digest_name(algorithm: string): string;
declare function crypto_hmac2(algorithm: string, key: Uint8Array, data: Uint8Array): Buffer | null;
declare function crypto_pbkdf2(password: Uint8Array, salt: Uint8Array, iterations: int, keylen: int, digest: string): Buffer | null;
declare function crypto_scrypt(password: Uint8Array, salt: Uint8Array, N: int, r: int, p: int, keylen: int, maxmem: int): Buffer | null;
declare function crypto_hkdf(digest: string, ikm: Uint8Array, salt: Uint8Array, info: Uint8Array, keylen: int): Buffer | null;
declare function crypto_cipher_new(kind: int, key: Uint8Array, iv: Uint8Array, decrypt: boolean): Uint8Array;
declare function crypto_cipher_update(state: Uint8Array, data: Uint8Array): Buffer;
declare function crypto_cipher_final(state: Uint8Array): Buffer | null;
declare function crypto_cipher_error(state: Uint8Array): int;
declare function crypto_cipher_padding(state: Uint8Array, on: boolean): void;
declare function crypto_cipher_aad(state: Uint8Array, aad: Uint8Array): void;
declare function crypto_cipher_set_tag(state: Uint8Array, tag: Uint8Array): void;
declare function crypto_cipher_tag_length(state: Uint8Array, n: int): void;
declare function crypto_cipher_tag(state: Uint8Array): Buffer;
declare function crypto_rsa_sign(digest: string, hash: Uint8Array, key: Uint8Array, salt: int): Buffer | null;
declare function crypto_rsa_verify(digest: string, hash: Uint8Array, key: Uint8Array, sig: Uint8Array, salt: int): boolean;
declare function crypto_rsa_keygen(bits: int, e: int): Uint8Array | null;
declare function crypto_ec_keygen(curve: int): Buffer | null;
declare function crypto_ec_public(curve: int, d: Uint8Array): Buffer | null;
declare function crypto_ec_sign(digest: string, hash: Uint8Array, curve: int, d: Uint8Array, raw: boolean): Buffer | null;
declare function crypto_ec_verify(hash: Uint8Array, curve: int, q: Uint8Array, sig: Uint8Array, raw: boolean): boolean;

// ---- errors with Node's codes ----

/** An Error with Node's code (ERR_CRYPTO_INVALID_DIGEST, ERR_OSSL_BAD_DECRYPT...). */
export class CryptoError extends Error {
  code: string;
  constructor(message: string, code: string, name: string = "Error") {
    super(message);
    this.name = name;
    this.code = code;
  }
}
function typeError(message: string, code: string): CryptoError {
  return new CryptoError(message, code, "TypeError");
}
function rangeError(message: string, code: string): CryptoError {
  return new CryptoError(message, code, "RangeError");
}
const DECODER_UNSUPPORTED = "error:1E08010C:DECODER routines::unsupported";

/** A random version 4 UUID, as 36 lowercase characters (8-4-4-4-12). */
export function randomUUID(): string {
  return crypto_uuid();
}

/** The digest algorithms createHash, createHmac, pbkdf2 and hkdf take. */
export function getHashes(): string[] {
  return ["md5", "sha1", "sha224", "sha256", "sha384", "sha512"];
}

/** The ciphers createCipheriv and createDecipheriv take. */
export function getCiphers(): string[] {
  const out: string[] = [];
  for (const bits of ["128", "192", "256"]) {
    for (const mode of ["cbc", "ctr", "ecb", "gcm"]) out.push(`aes-${bits}-${mode}`);
  }
  return out;
}

/** The named curves of EC keys. */
export function getCurves(): string[] {
  return ["prime256v1", "secp384r1", "secp521r1"];
}

// "SHA256", "RSA-SHA256", "sha-256": Node's (OpenSSL's) names of the same digest ("" for none of the six)
function digestName(algorithm: string): string {
  return crypto_digest_name(algorithm);
}

function bytesOf(data: string | Uint8Array, encoding: string): Uint8Array {
  if (typeof data === "string") return Buffer.from(data as string, encoding);
  return data as Uint8Array;
}

function isUtf8(encoding: string): boolean {
  return encoding === "utf8" || encoding === "utf-8" || encoding === "UTF-8" || encoding === "UTF8";
}

function concatBytes(parts: Uint8Array[]): Uint8Array {
  let n: int = 0;
  for (const p of parts) n += p.length;
  const out = new Uint8Array(n);
  let k: int = 0;
  for (const p of parts) {
    out.set(p, k);
    k += p.length;
  }
  return out;
}

// ---- hashes and HMACs ----
// (their errors are plain Errors: an Error subclass with a code costs a program that only hashes ~40 KB)

// what update() was given: text while it is only UTF-8 text (hashed by xisto_crypto.h's C), else bytes
class Pieces {
  text: string[] = [];
  bytes: Uint8Array[] | null = null;
  add(data: string | Uint8Array, encoding: string): void {
    const b = this.bytes;
    if (b === null && typeof data === "string" && isUtf8(encoding)) {
      this.text.push(data as string);
      return;
    }
    this.toBytes().push(bytesOf(data, encoding));
  }
  toBytes(): Uint8Array[] {
    const b = this.bytes;
    if (b !== null) return b;
    const list: Uint8Array[] = [];
    if (this.text.length > 0) list.push(Buffer.from(this.text.join(""), "utf8"));
    this.text = [];
    this.bytes = list;
    return list;
  }
  all(): Uint8Array {
    return concatBytes(this.toBytes());
  }
  copy(): Pieces {
    const p = new Pieces();
    for (const t of this.text) p.text.push(t);
    const b = this.bytes;
    if (b !== null) {
      const list: Uint8Array[] = [];
      for (const x of b) list.push(x);
      p.bytes = list;
    }
    return p;
  }
}

/** A hash being fed: update() as many times as needed, then digest() once. */
export class Hash {
  private algorithm: string;
  private pieces: Pieces = new Pieces();
  private finished: boolean = false;
  constructor(algorithm: string) {
    this.algorithm = algorithm;
  }
  /** Adds data: text (in inputEncoding, UTF-8 by default) or bytes. */
  update(data: string | Uint8Array, inputEncoding: string = "utf8"): Hash {
    if (this.finished) throw new Error("Digest already called");
    this.pieces.add(data, inputEncoding);
    return this;
  }
  private bytes(): Buffer {
    if (this.finished) throw new Error("Digest already called");
    this.finished = true;
    const p = this.pieces;
    if (p.bytes === null) return Buffer.from(crypto_digest(this.algorithm, p.text.join(""), "hex"), "hex");
    return crypto_hash2(this.algorithm, p.all())!;
  }
  /** The digest of everything given to update(), in encoding (hex, base64, base64url or latin1). */
  digest(encoding: string): string {
    if (this.finished) throw new Error("Digest already called");
    this.finished = true;
    const p = this.pieces;
    if (p.bytes === null) return crypto_digest(this.algorithm, p.text.join(""), encoding);
    return crypto_hash2_text(this.algorithm, p.all(), encoding);
  }
  // digest() with no encoding: the bytes, a Buffer (the compiler calls this for it)
  __digest_bytes(): Buffer {
    return this.bytes();
  }
  /** A new Hash in the same state (what was given so far), to be fed and digested on its own. */
  copy(): Hash {
    if (this.finished) throw new Error("Digest already called");
    const h = new Hash(this.algorithm);
    h.pieces = this.pieces.copy();
    return h;
  }
}

/** An HMAC being fed: update() as many times as needed, then digest() once. */
export class Hmac {
  private algorithm: string;
  private key: string;
  private keyBytes: Uint8Array | null = null; // (a key given as bytes: a Buffer, or a secret KeyObject)
  private pieces: Pieces = new Pieces();
  private finished: boolean = false;
  constructor(algorithm: string, key: string | Uint8Array | KeyObject) {
    this.algorithm = algorithm;
    this.key = "";
    if (typeof key === "string") {
      this.key = key as string;
    } else if (key instanceof KeyObject) {
      this.keyBytes = (key as KeyObject).secretBytes();
    } else {
      this.keyBytes = key as Uint8Array;
    }
  }
  private bytes(): Buffer {
    if (this.finished) throw new Error("Digest already called");
    this.finished = true;
    const kb = this.keyBytes;
    const key: Uint8Array = kb !== null ? kb : Buffer.from(this.key, "utf8");
    const p = this.pieces;
    if (p.bytes === null) return Buffer.from(crypto_hmac_bytes(this.algorithm, key, p.text.join("")));
    return crypto_hmac2(this.algorithm, key, p.all())!;
  }
  /** Adds data: text (in inputEncoding, UTF-8 by default) or bytes. */
  update(data: string | Uint8Array, inputEncoding: string = "utf8"): Hmac {
    if (this.finished) throw new Error("Digest already called");
    this.pieces.add(data, inputEncoding);
    return this;
  }
  /** The HMAC of everything given to update(), in encoding (hex, base64, base64url or latin1). */
  digest(encoding: string): string {
    if (this.finished) throw new Error("Digest already called");
    this.finished = true;
    const p = this.pieces;
    const kb = this.keyBytes;
    if (kb === null && p.bytes === null) return crypto_hmac(this.algorithm, this.key, p.text.join(""), encoding);
    return crypto_hmac2_text(this.algorithm, kb !== null ? kb : Buffer.from(this.key, "utf8"), p.all(), encoding);
  }
  // digest() with no encoding: the bytes, a Buffer (the compiler calls this for it)
  __digest_bytes(): Buffer {
    return this.bytes();
  }
}

/** A hash of the given algorithm (md5, sha1, sha224, sha256, sha384, sha512). Throws for any other. */
export function createHash(algorithm: string): Hash {
  const a = digestName(algorithm);
  if (a === "") throw new Error("Digest method not supported");
  return new Hash(a);
}

/** An HMAC of the given algorithm with key (UTF-8 text, bytes, or a secret KeyObject). */
export function createHmac(algorithm: string, key: string | Uint8Array | KeyObject): Hmac {
  const a = digestName(algorithm);
  if (a === "") throw new Error("Invalid digest: " + algorithm);
  return new Hmac(a, key);
}

/** The digest of data in one call (Node 21's crypto.hash): hex unless outputEncoding says otherwise. */
export function hash(algorithm: string, data: string | Uint8Array, outputEncoding: string = "hex"): string {
  return createHash(algorithm).update(data).digest(outputEncoding);
}

// ---- comparison and randomness ----

/** Compares two buffers in constant time; their lengths must be equal (RangeError otherwise, as Node). */
export function timingSafeEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) throw rangeError("Input buffers must have the same byte length", "ERR_CRYPTO_TIMING_SAFE_EQUAL_LENGTH");
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff = diff | (a[i] ^ b[i]);
  return diff === 0;
}

/** n random bytes, a Buffer. */
export function randomBytes(n: int): Buffer {
  return Buffer.from(crypto_random_bytes(n));
}

/** Fills a Uint8Array with random bytes and returns it (webcrypto's getRandomValues; at most 65536 bytes). */
export function getRandomValues(array: Uint8Array): Uint8Array {
  if (array.length > 65536) {
    throw new CryptoError(
      `Failed to execute 'getRandomValues' on 'Crypto': The ArrayBufferView's byte length (${array.length}) exceeds the number of bytes of entropy available via this API (65536)`,
      "QuotaExceededError",
      "QuotaExceededError"
    );
  }
  array.set(crypto_random_bytes(array.length), 0);
  return array;
}

/** Fills buf with random bytes (all of it, or size bytes from offset) and returns it. */
export function randomFillSync(buf: Uint8Array, offset: int = 0, size: int = -1): Uint8Array {
  const n = size < 0 ? buf.length - offset : size;
  buf.set(crypto_random_bytes(n), offset);
  return buf;
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

/** node:crypto's constants: the RSA paddings and PSS salt lengths. */
export class CryptoConstants {
  readonly RSA_NO_PADDING: int = 3;
  readonly RSA_PKCS1_PADDING: int = 1;
  readonly RSA_PKCS1_OAEP_PADDING: int = 4;
  readonly RSA_PKCS1_PSS_PADDING: int = 6;
  readonly RSA_PSS_SALTLEN_DIGEST: int = -1;
  readonly RSA_PSS_SALTLEN_MAX_SIGN: int = -2;
  readonly RSA_PSS_SALTLEN_AUTO: int = -2;
}
export const constants: CryptoConstants = new CryptoConstants();

// ---- key derivation ----

function checkDigest(digest: string): string {
  const a = digestName(digest);
  if (a === "") throw typeError("Invalid digest: " + digest, "ERR_CRYPTO_INVALID_DIGEST");
  return a;
}

/** PBKDF2 (RFC 8018) with HMAC over digest: keylen bytes. */
export function pbkdf2Sync(password: string | Uint8Array, salt: string | Uint8Array, iterations: int, keylen: int, digest: string): Buffer {
  const a = checkDigest(digest);
  if (iterations < 1 || iterations > 2147483647) {
    throw rangeError(`The value of "iterations" is out of range. It must be >= 1 && <= 2147483647. Received ${iterations}`, "ERR_OUT_OF_RANGE");
  }
  if (keylen < 0 || keylen > 2147483647) {
    throw rangeError(`The value of "keylen" is out of range. It must be >= 0 && <= 2147483647. Received ${keylen}`, "ERR_OUT_OF_RANGE");
  }
  return crypto_pbkdf2(bytesOf(password, "utf8"), bytesOf(salt, "utf8"), iterations, keylen, a)!;
}

/** The callback of pbkdf2, scrypt and generateKeyPair-style functions: an error, or the result. */
export type KeyCallback = (err: Error | null, derivedKey: Buffer) => void;

function later(f: () => Buffer, callback: KeyCallback): void {
  let err: Error | null = null;
  let out: Buffer = Buffer.alloc(0);
  try {
    out = f();
  } catch (e) {
    err = e as Error;
  }
  setTimeout(() => {
    callback(err, out);
  }, 0);
}

/** pbkdf2Sync, its result given to callback on a later turn of the event loop (bad arguments throw at once, as Node). */
export function pbkdf2(password: string | Uint8Array, salt: string | Uint8Array, iterations: int, keylen: int, digest: string, callback: KeyCallback): void {
  const key = pbkdf2Sync(password, salt, iterations, keylen, digest);
  later(() => key, callback);
}

/** scrypt's cost parameters (Node's names and their aliases). */
export class ScryptOptions {
  N?: int;
  r?: int;
  p?: int;
  cost?: int;
  blockSize?: int;
  parallelization?: int;
  maxmem?: int;
}

/** scrypt (RFC 7914): keylen bytes; N (cost, a power of two), r and p as options (16384, 8, 1 by default). */
export function scryptSync(password: string | Uint8Array, salt: string | Uint8Array, keylen: int, options: ScryptOptions | null = null): Buffer {
  const o: ScryptOptions = options !== null ? options : new ScryptOptions();
  const N = o.N ?? o.cost ?? 16384;
  const r = o.r ?? o.blockSize ?? 8;
  const p = o.p ?? o.parallelization ?? 1;
  const maxmem = o.maxmem ?? 33554432;
  if (N < 2 || (N & (N - 1)) !== 0) throw rangeError("Invalid scrypt params", "ERR_CRYPTO_INVALID_SCRYPT_PARAMS");
  const out = crypto_scrypt(bytesOf(password, "utf8"), bytesOf(salt, "utf8"), N, r, p, keylen, maxmem);
  if (out === null) {
    throw rangeError("Invalid scrypt params: error:030000AC:digital envelope routines::memory limit exceeded", "ERR_CRYPTO_INVALID_SCRYPT_PARAMS");
  }
  return out;
}

/** scryptSync, its result given to callback on a later turn of the event loop (options may be left out). */
export function scrypt(
  password: string | Uint8Array,
  salt: string | Uint8Array,
  keylen: int,
  options: ScryptOptions | KeyCallback,
  callback: KeyCallback | null = null
): void {
  if (callback !== null) {
    const o = options as ScryptOptions;
    later(() => scryptSync(password, salt, keylen, o), callback);
  } else {
    later(() => scryptSync(password, salt, keylen, null), options as KeyCallback);
  }
}

/** HKDF (RFC 5869) with digest: keylen bytes (Node gives an ArrayBuffer; xisto a Buffer, which Buffer.from takes too). */
export function hkdfSync(
  digest: string,
  ikm: string | Uint8Array | KeyObject,
  salt: string | Uint8Array,
  info: string | Uint8Array,
  keylen: int
): Buffer {
  const a = checkDigest(digest);
  const len = crypto_hash2(a, new Uint8Array(0))!.length;
  if (keylen > 255 * len) throw rangeError("Invalid key length", "ERR_CRYPTO_INVALID_KEYLEN");
  const k: Uint8Array = ikm instanceof KeyObject ? (ikm as KeyObject).secretBytes() : bytesOf(ikm as string | Uint8Array, "utf8");
  return crypto_hkdf(a, k, bytesOf(salt, "utf8"), bytesOf(info, "utf8"), keylen)!;
}

/** hkdfSync, its result given to callback on a later turn of the event loop. */
export function hkdf(
  digest: string,
  ikm: string | Uint8Array | KeyObject,
  salt: string | Uint8Array,
  info: string | Uint8Array,
  keylen: int,
  callback: KeyCallback
): void {
  later(() => hkdfSync(digest, ikm, salt, info, keylen), callback);
}

// ---- AES ----

/** The options of createCipheriv/createDecipheriv: GCM's tag length (16 by default). */
export class CipherOptions {
  authTagLength?: int;
}

/** A cipher or decipher being fed: update() gives what is ready, final() the rest (CBC: the padded last block). */
export class Cipher {
  private readonly state: Uint8Array;
  private readonly decrypt: boolean;
  private readonly gcm: boolean;
  private finished: boolean = false;
  private tagReady: boolean = false;
  constructor(kind: int, key: Uint8Array, iv: Uint8Array, decrypt: boolean, tagLength: int) {
    this.state = crypto_cipher_new(kind, key, iv, decrypt);
    this.decrypt = decrypt;
    this.gcm = kind === 3;
    if (this.gcm) crypto_cipher_tag_length(this.state, tagLength);
  }
  private run(data: string | Uint8Array, inputEncoding: string): Buffer {
    if (this.finished) throw new Error("Trying to add data in unsupported state");
    return crypto_cipher_update(this.state, bytesOf(data, inputEncoding));
  }
  /** More data (text in inputEncoding, UTF-8 by default, or bytes): what is ready, as text in outputEncoding. */
  update(data: string | Uint8Array, inputEncoding: string | null, outputEncoding: string): string {
    return this.text(this.run(data, inputEncoding ?? "utf8"), outputEncoding);
  }
  // update(data) / update(data, inputEncoding): a Buffer (the compiler calls this for them)
  __update_bytes(data: string | Uint8Array, inputEncoding: string | null = "utf8"): Buffer {
    return this.run(data, inputEncoding ?? "utf8");
  }
  // text output across calls, as Node's StringDecoder: a UTF-8 character cut between two updates, or base64's
  // bytes short of a group of three, wait for the next call
  private carry: Uint8Array = new Uint8Array(0);
  private text(b: Buffer, encoding: string): string {
    const utf8 = encoding === "utf8" || encoding === "utf-8";
    const b64 = encoding === "base64" || encoding === "base64url";
    if (!utf8 && !b64) return b.toString(encoding);
    const all = this.carry.length > 0 ? Buffer.from(concatBytes([this.carry, b])) : b;
    let end = all.length;
    if (b64) end = end - (end % 3);
    else {
      let back = 0;
      while (back < 4 && end - back - 1 >= 0 && (all[end - back - 1] & 0xc0) === 0x80) back++;
      if (end - back - 1 >= 0) {
        const lead = all[end - back - 1];
        const need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
        if (need > back + 1) end = end - back - 1;
      }
    }
    this.carry = Buffer.from(all.subarray(end, all.length));
    return all.subarray(0, end).toString(encoding);
  }
  private finish(): Buffer {
    if (this.finished) throw new CryptoError("Invalid state", "ERR_CRYPTO_INVALID_STATE");
    this.finished = true;
    const out = crypto_cipher_final(this.state);
    if (out !== null) {
      this.tagReady = this.gcm && !this.decrypt;
      return out;
    }
    const err = crypto_cipher_error(this.state);
    if (err === 2) throw new CryptoError("error:1C80006B:Provider routines::wrong final block length", "ERR_OSSL_WRONG_FINAL_BLOCK_LENGTH");
    if (err === 3) throw new CryptoError("error:1C800077:Provider routines::tag not set", "ERR_OSSL_TAG_NOT_SET");
    throw new CryptoError("error:1C800064:Provider routines::bad decrypt", "ERR_OSSL_BAD_DECRYPT");
  }
  /** The rest, as text in outputEncoding. */
  final(outputEncoding: string): string {
    const b = this.finish();
    if (this.carry.length > 0) {
      const all = Buffer.from(concatBytes([this.carry, b]));
      this.carry = new Uint8Array(0);
      return all.toString(outputEncoding);
    }
    return b.toString(outputEncoding);
  }
  // final(): a Buffer (the compiler calls this for it)
  __final_bytes(): Buffer {
    return this.finish();
  }
  /** setAutoPadding(false): CBC/ECB data must fill whole blocks (no PKCS#7 padding added or removed). */
  setAutoPadding(on: boolean = true): Cipher {
    crypto_cipher_padding(this.state, on);
    return this;
  }
  /** GCM: data authenticated but not encrypted, given before the first update(). */
  setAAD(aad: Uint8Array): Cipher {
    crypto_cipher_aad(this.state, aad);
    return this;
  }
  /** GCM, encrypting: the authentication tag, after final(). */
  getAuthTag(): Buffer {
    if (!this.tagReady) throw new CryptoError("Invalid state for operation getAuthTag", "ERR_CRYPTO_INVALID_STATE");
    return crypto_cipher_tag(this.state);
  }
  /** GCM, decrypting: the tag to check, before final(). */
  setAuthTag(tag: Uint8Array): Cipher {
    if (tag.length < 4 || tag.length > 16) throw typeError(`Invalid authentication tag length: ${tag.length}`, "ERR_CRYPTO_INVALID_AUTH_TAG");
    crypto_cipher_set_tag(this.state, tag);
    return this;
  }
}
/** A decipher: the same object as a cipher, running the other way. */
export type Decipher = Cipher;

function newCipher(algorithm: string, key: Uint8Array | KeyObject, iv: Uint8Array | null, decrypt: boolean, options: CipherOptions | null): Cipher {
  const alg = algorithm.toLowerCase();
  const parts = alg.split("-");
  const bits = parts.length === 3 && parts[0] === "aes" ? (parts[1] === "128" ? 16 : parts[1] === "192" ? 24 : parts[1] === "256" ? 32 : 0) : 0;
  const mode = parts.length === 3 ? parts[2] : "";
  const kind = mode === "cbc" ? 1 : mode === "ctr" ? 2 : mode === "gcm" ? 3 : mode === "ecb" ? 4 : 0;
  if (bits === 0 || kind === 0) throw new CryptoError("Unknown cipher", "ERR_CRYPTO_UNKNOWN_CIPHER");
  const k: Uint8Array = key instanceof KeyObject ? (key as KeyObject).secretBytes() : (key as Uint8Array);
  if (k.length !== bits) throw rangeError("Invalid key length", "ERR_CRYPTO_INVALID_KEYLEN");
  const v: Uint8Array = iv !== null ? iv : new Uint8Array(0);
  if ((kind === 1 || kind === 2) && v.length !== 16) throw typeError("Invalid initialization vector", "ERR_CRYPTO_INVALID_IV");
  if (kind === 3 && v.length === 0) throw typeError("Invalid initialization vector", "ERR_CRYPTO_INVALID_IV");
  if (kind === 4 && v.length !== 0) throw typeError("Invalid initialization vector", "ERR_CRYPTO_INVALID_IV");
  const tagLength = options !== null ? (options.authTagLength ?? 16) : 16;
  return new Cipher(kind, k, v, decrypt, tagLength);
}

/** A cipher: aes-128/192/256 in cbc, ctr (16-byte IV), gcm (any IV length; 12 is usual) or ecb (iv null). */
export function createCipheriv(algorithm: string, key: Uint8Array | KeyObject, iv: Uint8Array | null, options: CipherOptions | null = null): Cipher {
  return newCipher(algorithm, key, iv, false, options);
}

/** A decipher for what createCipheriv made with the same algorithm, key and IV. */
export function createDecipheriv(algorithm: string, key: Uint8Array | KeyObject, iv: Uint8Array | null, options: CipherOptions | null = null): Cipher {
  return newCipher(algorithm, key, iv, true, options);
}

// ---- DER and PEM ----

class Der {
  b: Uint8Array;
  at: int = 0;
  constructor(b: Uint8Array) {
    this.b = b;
  }
  done(): boolean {
    return this.at >= this.b.length;
  }
  peek(): int {
    return this.at < this.b.length ? this.b[this.at] : -1;
  }
  // a TLV's body (its tag must be tag)
  next(tag: int): Uint8Array {
    if (this.at + 2 > this.b.length || this.b[this.at] !== tag) throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
    this.at++;
    let len = this.b[this.at];
    this.at++;
    if (len >= 0x80) {
      const n = len - 0x80;
      if (n > 4) throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
      len = 0;
      for (let i = 0; i < n; i++) {
        len = len * 256 + this.b[this.at];
        this.at++;
      }
    }
    if (this.at + len > this.b.length) throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
    const body = this.b.subarray(this.at, this.at + len);
    this.at += len;
    return body;
  }
  // an INTEGER's magnitude, without the leading zero DER adds for a positive number
  uint(): Uint8Array {
    const v = this.next(0x02);
    let i = 0;
    while (i < v.length - 1 && v[i] === 0) i++;
    return v.subarray(i, v.length);
  }
}

// the body of b's first TLV (tag)
function derBody(b: Uint8Array, tag: int): Uint8Array {
  const d = new Der(b);
  return d.next(tag);
}
// the body of b's first TLV (tag), to be read in turn (locals, not temporaries: released when next() throws)
function derIn(b: Uint8Array, tag: int): Der {
  const d = new Der(b);
  const body = d.next(tag);
  return new Der(body);
}

function derLen(n: int): Uint8Array {
  if (n < 0x80) return Uint8Array.of(n);
  if (n < 0x100) return Uint8Array.of(0x81, n);
  if (n < 0x10000) return Uint8Array.of(0x82, n >> 8, n & 0xff);
  return Uint8Array.of(0x83, n >> 16, (n >> 8) & 0xff, n & 0xff);
}
function tlv(tag: int, body: Uint8Array): Uint8Array {
  return concatBytes([Uint8Array.of(tag), derLen(body.length), body]);
}
function seq(parts: Uint8Array[]): Uint8Array {
  return tlv(0x30, concatBytes(parts));
}
function derUint(v: Uint8Array): Uint8Array {
  let i = 0;
  while (i < v.length - 1 && v[i] === 0) i++;
  const m = v.subarray(i, v.length);
  if (m.length > 0 && (m[0] & 0x80) !== 0) return tlv(0x02, concatBytes([Uint8Array.of(0), m]));
  return tlv(0x02, m);
}
function derSmall(n: int): Uint8Array {
  return tlv(0x02, Uint8Array.of(n));
}
function hexBytes(h: string): Uint8Array {
  return Buffer.from(h, "hex");
}
function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

const OID_RSA = "06092a864886f70d010101";
const OID_EC = "06072a8648ce3d0201";
const NULL_PARAMS = "0500";
// curve id (BearSSL's, TLS's) -> OID, Node's name, JWK's name, coordinate size
function curveOid(curve: int): string {
  if (curve === 23) return "06082a8648ce3d030107";
  if (curve === 24) return "06052b81040022";
  return "06052b81040023";
}
function curveName(curve: int): string {
  if (curve === 23) return "prime256v1";
  if (curve === 24) return "secp384r1";
  return "secp521r1";
}
function curveJwk(curve: int): string {
  if (curve === 23) return "P-256";
  if (curve === 24) return "P-384";
  return "P-521";
}
function curveSize(curve: int): int {
  if (curve === 23) return 32;
  if (curve === 24) return 48;
  return 66;
}
function curveOfOid(oid: Uint8Array): int {
  const h = Buffer.from(oid).toString("hex");
  if (h === "2a8648ce3d030107") return 23;
  if (h === "2b81040022") return 24;
  if (h === "2b81040023") return 25;
  throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
}
function curveOfName(name: string): int {
  if (name === "prime256v1" || name === "P-256" || name === "secp256r1") return 23;
  if (name === "secp384r1" || name === "P-384") return 24;
  if (name === "secp521r1" || name === "P-521") return 25;
  throw typeError("Invalid EC curve name", "ERR_CRYPTO_INVALID_CURVE");
}

class Pem {
  label: string = "";
  der: Uint8Array = new Uint8Array(0);
}
function pemDecode(text: string): Pem {
  const p = new Pem();
  const begin = text.indexOf("-----BEGIN ");
  if (begin < 0) throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
  const labelEnd = text.indexOf("-----", begin + 11);
  if (labelEnd < 0) throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
  p.label = text.slice(begin + 11, labelEnd);
  const end = text.indexOf("-----END " + p.label + "-----", labelEnd);
  if (end < 0) throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
  let b64 = "";
  for (const line of text.slice(labelEnd + 5, end).split("\n")) {
    const t = line.trim();
    if (t.length > 0 && !t.includes(":")) b64 += t;
  }
  p.der = Buffer.from(b64, "base64");
  return p;
}
function pemEncode(label: string, der: Uint8Array): string {
  const b64 = Buffer.from(der).toString("base64");
  let out = "-----BEGIN " + label + "-----\n";
  for (let i = 0; i < b64.length; i += 64) out += b64.slice(i, i + 64) + "\n";
  return out + "-----END " + label + "-----\n";
}

// packed key numbers (xisto_crypto.h): each a 4-byte big-endian length and its bytes
function pack(fields: Uint8Array[]): Uint8Array {
  const parts: Uint8Array[] = [];
  for (const f of fields) {
    let i = 0;
    while (i < f.length - 1 && f[i] === 0) i++;
    const m = f.subarray(i, f.length);
    parts.push(Uint8Array.of((m.length >> 24) & 0xff, (m.length >> 16) & 0xff, (m.length >> 8) & 0xff, m.length & 0xff));
    parts.push(m);
  }
  return concatBytes(parts);
}
function unpack(b: Uint8Array): Uint8Array[] {
  const out: Uint8Array[] = [];
  let at: int = 0;
  while (at + 4 <= b.length) {
    const n = b[at] * 16777216 + b[at + 1] * 65536 + b[at + 2] * 256 + b[at + 3];
    out.push(b.subarray(at + 4, at + 4 + n));
    at += 4 + n;
  }
  return out;
}

function b64url(b: Uint8Array): string {
  return Buffer.from(b).toString("base64url");
}
function unb64url(s: string): Uint8Array {
  return Buffer.from(s, "base64url");
}
function padTo(b: Uint8Array, n: int): Uint8Array {
  if (b.length >= n) return b.subarray(b.length - n, b.length);
  const out = new Uint8Array(n);
  out.set(b, n - b.length);
  return out;
}

/** A key as JSON (RFC 7517): RSA (n, e and the private numbers), EC (crv, x, y, d) or a secret (k). */
export class JsonWebKey {
  kty: string = "";
  n?: string;
  e?: string;
  d?: string;
  p?: string;
  q?: string;
  dp?: string;
  dq?: string;
  qi?: string;
  x?: string;
  y?: string;
  crv?: string;
  k?: string;
  alg?: string;
  use?: string;
  kid?: string;
}

/** asymmetricKeyDetails: modulusLength and publicExponent (RSA), namedCurve (EC). */
export class AsymmetricKeyDetails {
  modulusLength?: int;
  publicExponent?: bigint;
  namedCurve?: string;
}

/** What export() takes: type (spki, pkcs1, pkcs8, sec1) and format (pem, der, jwk). */
export class KeyExportOptions {
  type?: string;
  format?: string;
  cipher?: string;
  passphrase?: string;
}

/** A key: public or private (RSA, EC), or secret (bytes for HMAC and ciphers). */
export class KeyObject {
  readonly type: string;
  // rsa: n e (public) or n e d p q dp dq qi (private), packed; ec: the curve, d (private) and the point q
  rsa: Uint8Array = new Uint8Array(0);
  curve: int = 0;
  d: Uint8Array = new Uint8Array(0);
  q: Uint8Array = new Uint8Array(0);
  secret: Uint8Array = new Uint8Array(0);
  private kind: string;
  constructor(type: string, kind: string) {
    this.type = type;
    this.kind = kind;
  }
  /** rsa or ec for a key pair's key; null for a secret key. */
  get asymmetricKeyType(): string | null {
    return this.kind === "" ? null : this.kind;
  }
  /** The bytes of a secret key (null for a public or private one). */
  get symmetricKeySize(): int | null {
    return this.type === "secret" ? this.secret.length : null;
  }
  /** RSA: modulusLength and publicExponent; EC: namedCurve. */
  get asymmetricKeyDetails(): AsymmetricKeyDetails | null {
    if (this.kind === "rsa") {
      const f = unpack(this.rsa);
      const n = f[0];
      let bits = n.length * 8;
      for (let top = n[0]; top < 128 && bits > 0; top = top * 2) bits--;
      let e = 0n;
      for (let i = 0; i < f[1].length; i++) e = e * 256n + BigInt(f[1][i]);
      return { modulusLength: bits, publicExponent: e };
    }
    if (this.kind === "ec") return { namedCurve: curveName(this.curve) };
    return null;
  }
  secretBytes(): Uint8Array {
    if (this.type !== "secret") throw typeError(`Invalid key object type ${this.type}, expected secret.`, "ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE");
    return this.secret;
  }
  // DER of the key in a format: spki/pkcs1 (public), pkcs8/pkcs1/sec1 (private)
  private der(type: string): Uint8Array {
    if (this.type === "secret") return Buffer.from(this.secret);
    const f = unpack(this.rsa);
    if (this.kind === "rsa" && this.type === "public") {
      const pkcs1 = seq([derUint(f[0]), derUint(f[1])]);
      if (type === "pkcs1") return pkcs1;
      if (type !== "spki") throw keyTypeError(type, "public", "rsa");
      return seq([seq([hexBytes(OID_RSA), hexBytes(NULL_PARAMS)]), tlv(0x03, concatBytes([Uint8Array.of(0), pkcs1]))]);
    }
    if (this.kind === "rsa") {
      const nums: Uint8Array[] = [derSmall(0)];
      for (const x of f) nums.push(derUint(x));
      const pkcs1 = seq(nums);
      if (type === "pkcs1") return pkcs1;
      if (type !== "pkcs8") throw keyTypeError(type, "private", "rsa");
      return seq([derSmall(0), seq([hexBytes(OID_RSA), hexBytes(NULL_PARAMS)]), tlv(0x04, pkcs1)]);
    }
    const point = tlv(0x03, concatBytes([Uint8Array.of(0), this.q]));
    const algo = seq([hexBytes(OID_EC), hexBytes(curveOid(this.curve))]);
    if (this.type === "public") {
      if (type !== "spki") throw keyTypeError(type, "public", "ec");
      return seq([algo, point]);
    }
    const dpad = padTo(this.d, curveSize(this.curve));
    if (type === "sec1") return seq([derSmall(1), tlv(0x04, dpad), tlv(0xa0, hexBytes(curveOid(this.curve))), tlv(0xa1, point)]);
    if (type !== "pkcs8") throw keyTypeError(type, "private", "ec");
    return seq([derSmall(0), algo, tlv(0x04, seq([derSmall(1), tlv(0x04, dpad), tlv(0xa1, point)]))]);
  }
  /** The key as PEM text (options.format "pem", the default for public and private keys). */
  export(options: KeyExportOptions | null = null): string {
    const o: KeyExportOptions = options !== null ? options : new KeyExportOptions();
    if (this.type === "secret") return Buffer.from(this.secret).toString("latin1");
    const type = o.type ?? "";
    if (type === "") throw typeError(`The property 'options.type' is invalid. Received undefined`, "ERR_INVALID_ARG_VALUE");
    const der = this.der(type);
    if (type === "pkcs1") return pemEncode(this.type === "public" ? "RSA PUBLIC KEY" : "RSA PRIVATE KEY", der);
    if (type === "sec1") return pemEncode("EC PRIVATE KEY", der);
    return pemEncode(this.type === "public" ? "PUBLIC KEY" : "PRIVATE KEY", der);
  }
  // export({ format: "der" }) and a secret key's export(): bytes (the compiler calls this for them)
  __export_der(options: KeyExportOptions | null = null): Buffer {
    if (this.type === "secret") return Buffer.from(this.secret);
    const o: KeyExportOptions = options !== null ? options : new KeyExportOptions();
    return Buffer.from(this.der(o.type ?? ""));
  }
  // export({ format: "jwk" }): the key as a JsonWebKey (the compiler calls this for it)
  __export_jwk(options: KeyExportOptions | null = null): JsonWebKey {
    const j = new JsonWebKey();
    if (this.type === "secret") {
      j.kty = "oct";
      j.k = b64url(this.secret);
      return j;
    }
    if (this.kind === "rsa") {
      const f = unpack(this.rsa);
      j.kty = "RSA";
      j.n = b64url(f[0]);
      j.e = b64url(f[1]);
      if (this.type === "private") {
        j.d = b64url(f[2]);
        j.p = b64url(f[3]);
        j.q = b64url(f[4]);
        j.dp = b64url(f[5]);
        j.dq = b64url(f[6]);
        j.qi = b64url(f[7]);
      }
      return j;
    }
    const size = curveSize(this.curve);
    j.kty = "EC";
    j.x = b64url(this.q.subarray(1, 1 + size));
    j.y = b64url(this.q.subarray(1 + size, 1 + 2 * size));
    j.crv = curveJwk(this.curve);
    if (this.type === "private") j.d = b64url(padTo(this.d, size));
    return j;
  }
  /** Whether other is the same key (same type and numbers). */
  equals(other: KeyObject): boolean {
    if (this.type !== other.type || this.kind !== other.kind) return false;
    if (this.type === "secret") return sameBytes(this.secret, other.secret);
    if (this.kind === "rsa") return sameBytes(this.rsa, other.rsa);
    return this.curve === other.curve && sameBytes(this.q, other.q) && sameBytes(this.d, other.d);
  }
  // the public half of a private key
  publicOf(): KeyObject {
    if (this.type === "public") return this;
    const k = new KeyObject("public", this.kind);
    if (this.kind === "rsa") {
      const f = unpack(this.rsa);
      k.rsa = pack([f[0], f[1]]);
    } else {
      k.curve = this.curve;
      k.q = this.q;
    }
    return k;
  }
}

function keyTypeError(type: string, which: string, kind: string): CryptoError {
  return typeError(`The property 'options.type' is invalid. Received '${type}'`, "ERR_INVALID_ARG_VALUE");
}

function rsaPublic(n: Uint8Array, e: Uint8Array): KeyObject {
  const k = new KeyObject("public", "rsa");
  k.rsa = pack([n, e]);
  return k;
}
function ecPublic(curve: int, q: Uint8Array): KeyObject {
  const k = new KeyObject("public", "ec");
  k.curve = curve;
  k.q = Buffer.from(q);
  return k;
}
function ecPrivate(curve: int, d: Uint8Array, q: Uint8Array | null): KeyObject {
  const k = new KeyObject("private", "ec");
  k.curve = curve;
  k.d = Buffer.from(d);
  k.q = q !== null ? Buffer.from(q) : crypto_ec_public(curve, d)!;
  return k;
}
function rsaPrivateKey(der: Der): KeyObject {
  der.uint(); // version
  const f: Uint8Array[] = [];
  for (let i = 0; i < 8; i++) f.push(der.uint());
  const k = new KeyObject("private", "rsa");
  k.rsa = pack(f);
  return k;
}
// SEC1's ECPrivateKey; curve from the PKCS#8 algorithm when the key itself leaves it out
function ecPrivateKey(der: Der, curveFromAlg: int): KeyObject {
  der.uint(); // version 1
  const d = der.next(0x04);
  let curve = curveFromAlg;
  let q: Uint8Array | null = null;
  while (!der.done()) {
    const tag = der.peek();
    const body = der.next(tag);
    if (tag === 0xa0) curve = curveOfOid(derBody(body, 0x06));
    else if (tag === 0xa1) {
      const bits = derBody(body, 0x03);
      q = bits.subarray(1, bits.length);
    }
  }
  if (curve === 0) throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
  return ecPrivate(curve, d, q);
}
function spki(body: Uint8Array): KeyObject {
  const outer = derIn(body, 0x30);
  const alg = new Der(outer.next(0x30));
  const oid = alg.next(0x06);
  const bits = outer.next(0x03);
  const key = bits.subarray(1, bits.length);
  const h = Buffer.from(oid).toString("hex");
  if (h === "2a864886f70d010101") {
    const r = derIn(key, 0x30);
    return rsaPublic(r.uint(), r.uint());
  }
  if (h === "2a8648ce3d0201") return ecPublic(curveOfOid(alg.next(0x06)), key);
  throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
}
function pkcs8(body: Uint8Array): KeyObject {
  const outer = derIn(body, 0x30);
  outer.uint(); // version
  const alg = new Der(outer.next(0x30));
  const oid = Buffer.from(alg.next(0x06)).toString("hex");
  const inner = outer.next(0x04);
  if (oid === "2a864886f70d010101") return rsaPrivateKey(derIn(inner, 0x30));
  if (oid === "2a8648ce3d0201") return ecPrivateKey(derIn(inner, 0x30), curveOfOid(alg.next(0x06)));
  throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
}
function fromPem(text: string, wantPrivate: boolean): KeyObject {
  const p = pemDecode(text);
  if (p.label === "PUBLIC KEY") return spki(p.der);
  if (p.label === "RSA PUBLIC KEY") {
    const r = derIn(p.der, 0x30);
    return rsaPublic(r.uint(), r.uint());
  }
  if (p.label === "PRIVATE KEY") return pkcs8(p.der);
  if (p.label === "RSA PRIVATE KEY") return rsaPrivateKey(derIn(p.der, 0x30));
  if (p.label === "EC PRIVATE KEY") return ecPrivateKey(derIn(p.der, 0x30), 0);
  if (p.label === "CERTIFICATE") throw new CryptoError("xisto: keys from certificates are not supported", "ERR_OSSL_UNSUPPORTED");
  throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
}
function fromDer(der: Uint8Array, type: string, wantPrivate: boolean): KeyObject {
  if (type === "spki") return spki(der);
  if (type === "pkcs8") return pkcs8(der);
  if (type === "sec1") return ecPrivateKey(derIn(der, 0x30), 0);
  if (type === "pkcs1") {
    if (wantPrivate) return rsaPrivateKey(derIn(der, 0x30));
    const r = derIn(der, 0x30);
    return rsaPublic(r.uint(), r.uint());
  }
  throw typeError(`The property 'key.type' is invalid. Received '${type}'`, "ERR_INVALID_ARG_VALUE");
}
function fromJwk(j: JsonWebKey, wantPrivate: boolean): KeyObject {
  if (j.kty === "RSA") {
    const n = unb64url(j.n ?? "");
    const e = unb64url(j.e ?? "");
    const d = j.d ?? "";
    if (!wantPrivate || d === "") return rsaPublic(n, e);
    const k = new KeyObject("private", "rsa");
    k.rsa = pack([n, e, unb64url(d), unb64url(j.p ?? ""), unb64url(j.q ?? ""), unb64url(j.dp ?? ""), unb64url(j.dq ?? ""), unb64url(j.qi ?? "")]);
    return k;
  }
  if (j.kty === "EC") {
    const curve = curveOfName(j.crv ?? "");
    const q = concatBytes([Uint8Array.of(4), padTo(unb64url(j.x ?? ""), curveSize(curve)), padTo(unb64url(j.y ?? ""), curveSize(curve))]);
    const d = j.d ?? "";
    if (!wantPrivate || d === "") return ecPublic(curve, q);
    return ecPrivate(curve, unb64url(d), q);
  }
  throw typeError("Invalid JWK data", "ERR_CRYPTO_INVALID_JWK");
}

/** What createPublicKey/createPrivateKey take besides PEM text: the key (PEM, DER bytes or a JWK) and its format. */
export class KeyInput {
  key: string | Uint8Array | JsonWebKey | KeyObject;
  format?: string;
  type?: string;
  encoding?: string;
  passphrase?: string;
}

function keyFrom(input: string | Uint8Array | KeyInput | KeyObject, wantPrivate: boolean): KeyObject {
  if (input instanceof KeyObject) return input as KeyObject;
  if (typeof input === "string") return fromPem(input as string, wantPrivate);
  if (input instanceof Uint8Array) return fromPem(Buffer.from(input as Uint8Array).toString("utf8"), wantPrivate);
  const o = input as KeyInput;
  if (o.passphrase != null) throw new CryptoError("xisto: encrypted private keys are not supported", "ERR_OSSL_UNSUPPORTED");
  const format = o.format ?? "pem";
  const key = o.key;
  if (key instanceof KeyObject) return key as KeyObject;
  if (format === "jwk") return fromJwk(key as JsonWebKey, wantPrivate);
  if (format === "der") return fromDer(key as Uint8Array, o.type ?? "", wantPrivate);
  if (typeof key === "string") return fromPem(key as string, wantPrivate);
  return fromPem(Buffer.from(key as Uint8Array).toString("utf8"), wantPrivate);
}

/** A public key from PEM (SPKI or PKCS#1; a private key's PEM gives its public half), DER, a JWK or a private KeyObject. */
export function createPublicKey(key: string | Uint8Array | KeyInput | KeyObject): KeyObject {
  const k = keyFrom(key, false);
  if (k.type === "secret") throw typeError("Invalid key object type secret, expected private.", "ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE");
  return k.publicOf();
}

/** A private key from PEM (PKCS#8, PKCS#1 or SEC1), DER or a JWK. */
export function createPrivateKey(key: string | Uint8Array | KeyInput | KeyObject): KeyObject {
  const k = keyFrom(key, true);
  if (k.type !== "private") throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
  return k;
}

/** A secret key (HMAC, ciphers) from bytes, or text in encoding. */
export function createSecretKey(key: string | Uint8Array, encoding: string = "utf8"): KeyObject {
  const k = new KeyObject("secret", "");
  k.secret = Buffer.from(bytesOf(key, encoding));
  return k;
}

// ---- key generation ----

/** The encoding of one key of a generated pair: type (spki, pkcs1, pkcs8, sec1) and format (pem). */
export class KeyEncoding {
  type: string;
  format: string;
  cipher?: string;
  passphrase?: string;
}

/** generateKeyPairSync's options: modulusLength and publicExponent (rsa), namedCurve (ec), the encodings. */
export class KeyPairOptions {
  modulusLength?: int;
  publicExponent?: int;
  namedCurve?: string;
  publicKeyEncoding?: KeyEncoding;
  privateKeyEncoding?: KeyEncoding;
}

/** A generated pair as KeyObjects. */
export class KeyPairKeyObjectResult {
  publicKey: KeyObject;
  privateKey: KeyObject;
  constructor(publicKey: KeyObject, privateKey: KeyObject) {
    this.publicKey = publicKey;
    this.privateKey = privateKey;
  }
}

/** A generated pair as PEM text (publicKeyEncoding and privateKeyEncoding given). */
export class KeyPairPemResult {
  publicKey: string;
  privateKey: string;
  constructor(publicKey: string, privateKey: string) {
    this.publicKey = publicKey;
    this.privateKey = privateKey;
  }
}

function generate(type: string, options: KeyPairOptions | null): KeyObject {
  const o: KeyPairOptions = options !== null ? options : new KeyPairOptions();
  if (type === "rsa") {
    const bits = o.modulusLength ?? 0;
    if (bits === 0) throw typeError(`The "options.modulusLength" property must be of type number. Received undefined`, "ERR_INVALID_ARG_TYPE");
    const packed = crypto_rsa_keygen(bits, o.publicExponent ?? 65537);
    if (packed === null) throw new CryptoError("xisto: RSA keys of " + bits + " bits are not supported (512 to 4096)", "ERR_OSSL_UNSUPPORTED");
    const k = new KeyObject("private", "rsa");
    k.rsa = Buffer.from(packed);
    return k;
  }
  if (type === "ec") {
    const name = o.namedCurve ?? "";
    if (name === "") throw typeError(`The "options.namedCurve" property must be of type string. Received undefined`, "ERR_INVALID_ARG_TYPE");
    const curve = curveOfName(name);
    return ecPrivate(curve, crypto_ec_keygen(curve)!, null);
  }
  throw typeError(`The argument 'type' must be a supported key type. Received '${type}'`, "ERR_INVALID_ARG_VALUE");
}

/** A new key pair: "rsa" (modulusLength, publicExponent) or "ec" (namedCurve: prime256v1/P-256, P-384, P-521). */
export function generateKeyPairSync(type: string, options: KeyPairOptions | null = null): KeyPairKeyObjectResult {
  const priv = generate(type, options);
  return new KeyPairKeyObjectResult(priv.publicOf(), priv);
}

// generateKeyPairSync with publicKeyEncoding/privateKeyEncoding: the keys as PEM (the compiler calls this for it)
export function __generateKeyPairSync_pem(type: string, options: KeyPairOptions): KeyPairPemResult {
  const priv = generate(type, options);
  const pe = options.publicKeyEncoding;
  const se = options.privateKeyEncoding;
  const pub = pe != null ? priv.publicOf().export({ type: pe.type, format: "pem" }) : "";
  const sec = se != null ? priv.export({ type: se.type, format: "pem" }) : "";
  return new KeyPairPemResult(pub, sec);
}

/** A new secret key: type "hmac" or "aes", options.length in bits. */
export class SecretKeyOptions {
  length: int;
}
export function generateKeySync(type: string, options: SecretKeyOptions): KeyObject {
  if (type !== "hmac" && type !== "aes") throw typeError(`The argument 'type' must be one of: 'aes', 'hmac'. Received '${type}'`, "ERR_INVALID_ARG_VALUE");
  return createSecretKey(crypto_random_bytes(Math.trunc(options.length / 8)));
}

// ---- signatures ----

/** A key for sign/verify with its options: dsaEncoding (der, ieee-p1363) for ECDSA, padding and saltLength for RSA. */
export class SignKeyInput {
  key: string | Uint8Array | KeyObject;
  dsaEncoding?: string;
  padding?: int;
  saltLength?: int;
  format?: string;
  type?: string;
  passphrase?: string;
}

function signKey(key: string | Uint8Array | KeyObject | SignKeyInput, wantPrivate: boolean): KeyObject {
  if (key instanceof SignKeyInput) {
    const o = key as SignKeyInput;
    const k = o.key;
    if (o.format === "der") return fromDer(k as Uint8Array, o.type ?? "", wantPrivate);
    return keyFrom(k as string | Uint8Array | KeyObject, wantPrivate);
  }
  return keyFrom(key as string | Uint8Array | KeyObject, wantPrivate);
}
function signOptions(key: string | Uint8Array | KeyObject | SignKeyInput): SignKeyInput | null {
  if (key instanceof SignKeyInput) return key as SignKeyInput;
  return null;
}

function digestOf(alg: string): string {
  return digestName(alg);
}

// the PSS salt length for signing: a length, or Node's -1 (the digest's) and -2 (the largest the key allows)
function pssSalt(k: KeyObject, digest: string, salt: int): int {
  const hl = crypto_hash2(digest, new Uint8Array(0))!.length;
  if (salt === -1) return hl;
  if (salt === -2) {
    const f = unpack(k.rsa);
    let bits = f[0].length * 8;
    for (let top = f[0][0]; top < 128 && bits > 0; top = top * 2) bits--;
    return Math.trunc((bits - 1 + 7) / 8) - hl - 2;
  }
  return salt;
}

function signWith(digest: string, data: Uint8Array, key: string | Uint8Array | KeyObject | SignKeyInput): Buffer {
  const k = signKey(key, true);
  if (k.type !== "private") throw new CryptoError(DECODER_UNSUPPORTED, "ERR_OSSL_UNSUPPORTED");
  const o = signOptions(key);
  const h = crypto_hash2(digest, data);
  if (h === null) throw typeError("Invalid digest", "ERR_CRYPTO_INVALID_DIGEST");
  if (k.asymmetricKeyType === "ec") {
    const raw = o !== null && o.dsaEncoding === "ieee-p1363";
    return crypto_ec_sign(digest, h, k.curve, k.d, raw)!;
  }
  const padding = o !== null ? (o.padding ?? 1) : 1;
  const salt = padding === 6 ? pssSalt(k, digest, o !== null ? (o.saltLength ?? -2) : -2) : -1;
  const sig = crypto_rsa_sign(digest, h, k.rsa, salt);
  if (sig === null) throw new CryptoError("error:0200006E:rsa routines::digest too big for rsa key", "ERR_OSSL_RSA_DIGEST_TOO_BIG_FOR_RSA_KEY");
  return sig;
}

function verifyWith(digest: string, data: Uint8Array, key: string | Uint8Array | KeyObject | SignKeyInput, signature: Uint8Array): boolean {
  const k = signKey(key, false).publicOf();
  const o = signOptions(key);
  const h = crypto_hash2(digest, data);
  if (h === null) throw typeError("Invalid digest", "ERR_CRYPTO_INVALID_DIGEST");
  if (k.asymmetricKeyType === "ec") {
    const raw = o !== null && o.dsaEncoding === "ieee-p1363";
    return crypto_ec_verify(h, k.curve, k.q, signature, raw);
  }
  const padding = o !== null ? (o.padding ?? 1) : 1;
  if (padding !== 6) return crypto_rsa_verify(digest, h, k.rsa, signature, -1);
  const salt = o !== null ? (o.saltLength ?? -2) : -2;
  if (salt >= 0 || salt === -1) return crypto_rsa_verify(digest, h, k.rsa, signature, pssSalt(k, digest, salt));
  // RSA_PSS_SALTLEN_AUTO: the lengths signers use (the largest, the digest's, none)
  const hl = crypto_hash2(digest, new Uint8Array(0))!.length;
  const lengths: int[] = [pssSalt(k, digest, -2), hl, 0];
  for (const s of lengths) if (s >= 0 && crypto_rsa_verify(digest, h, k.rsa, signature, s)) return true;
  return false;
}

/** A signature being fed: update() the data, then sign(privateKey) once. */
export class Sign {
  private readonly digest: string;
  private pieces: Pieces = new Pieces();
  constructor(digest: string) {
    this.digest = digest;
  }
  /** Adds data: text (in inputEncoding, UTF-8 by default) or bytes. */
  update(data: string | Uint8Array, inputEncoding: string = "utf8"): Sign {
    this.pieces.add(data, inputEncoding);
    return this;
  }
  /** The signature with the private key (PEM, KeyObject, or { key, dsaEncoding, padding, saltLength }) as text. */
  sign(privateKey: string | Uint8Array | KeyObject | SignKeyInput, outputEncoding: string): string {
    return this.__sign_bytes(privateKey).toString(outputEncoding);
  }
  // sign(key): a Buffer (the compiler calls this for it)
  __sign_bytes(privateKey: string | Uint8Array | KeyObject | SignKeyInput): Buffer {
    return signWith(this.digest, this.pieces.all(), privateKey);
  }
}

/** A signature check being fed: update() the data, then verify(publicKey, signature) once. */
export class Verify {
  private readonly digest: string;
  private pieces: Pieces = new Pieces();
  constructor(digest: string) {
    this.digest = digest;
  }
  /** Adds data: text (in inputEncoding, UTF-8 by default) or bytes. */
  update(data: string | Uint8Array, inputEncoding: string = "utf8"): Verify {
    this.pieces.add(data, inputEncoding);
    return this;
  }
  /** Whether signature (bytes, or text in signatureEncoding) is the data's, by the key (public, or a private key's public half). */
  verify(publicKey: string | Uint8Array | KeyObject | SignKeyInput, signature: string | Uint8Array, signatureEncoding: string = "base64"): boolean {
    const sig: Uint8Array = typeof signature === "string" ? Buffer.from(signature as string, signatureEncoding) : (signature as Uint8Array);
    return verifyWith(this.digest, this.pieces.all(), publicKey, sig);
  }
}

function signDigest(algorithm: string): string {
  const a = digestOf(algorithm);
  if (a === "" || a === "md5") throw typeError("Invalid digest", "ERR_CRYPTO_INVALID_DIGEST");
  return a;
}

/** A signer: RSA-SHA256, sha256, ecdsa-with-SHA256... (the digest names getHashes() lists but md5). */
export function createSign(algorithm: string): Sign {
  return new Sign(signDigest(algorithm));
}

/** A verifier for what createSign made with the same algorithm. */
export function createVerify(algorithm: string): Verify {
  return new Verify(signDigest(algorithm));
}

/** The signature of data in one call (algorithm null: sha256 for RSA and EC keys). */
export function sign(algorithm: string | null, data: Uint8Array, key: string | Uint8Array | KeyObject | SignKeyInput): Buffer {
  return signWith(signDigest(algorithm ?? "sha256"), data, key);
}

/** Whether signature is data's by key, in one call. */
export function verify(algorithm: string | null, data: Uint8Array, key: string | Uint8Array | KeyObject | SignKeyInput, signature: Uint8Array): boolean {
  return verifyWith(signDigest(algorithm ?? "sha256"), data, key, signature);
}

// ---- RSA encryption (publicEncrypt), over bigint ----

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

function mgf1(hash: string, seed: Uint8Array, len: int): Uint8Array {
  const out = new Uint8Array(len);
  let k: int = 0;
  let counter = 0;
  while (k < len) {
    const c = Uint8Array.of((counter >> 24) & 0xff, (counter >> 16) & 0xff, (counter >> 8) & 0xff, counter & 0xff);
    const d = crypto_hash2(hash, concatBytes([seed, c]))!;
    for (let i = 0; i < d.length && k < len; i++) {
      out[k] = d[i];
      k++;
    }
    counter++;
  }
  return out;
}

/** What publicEncrypt takes: the key (PEM or KeyObject) and the padding (constants.RSA_PKCS1_OAEP_PADDING by default). */
export class PublicKeyInput {
  key: string | KeyObject;
  padding?: int;
  oaepHash?: string;
}

/**
 * Encrypts a short buffer with an RSA public key: PKCS#1 v1.5 (constants.RSA_PKCS1_PADDING) or OAEP (the default,
 * SHA-1 unless oaepHash says otherwise).
 */
export function publicEncrypt(key: PublicKeyInput | string | KeyObject, data: Uint8Array): Buffer {
  let input: PublicKeyInput;
  if (typeof key === "string") input = { key: key as string };
  else if (key instanceof KeyObject) input = { key: key as KeyObject };
  else input = key as PublicKeyInput;
  const ko = input.key;
  const pub = (ko instanceof KeyObject ? (ko as KeyObject) : keyFrom(ko as string, false)).publicOf();
  if (pub.asymmetricKeyType !== "rsa") throw new CryptoError("error:1C8000A5:Provider routines::invalid key length", "ERR_OSSL_UNSUPPORTED");
  const f = unpack(pub.rsa);
  const n = toBig(f[0]);
  const e = toBig(f[1]);
  const size = f[0].length;
  const padding = input.padding ?? 4;
  const em = new Uint8Array(size);
  if (padding === 1) {
    if (data.length > size - 11) throw new Error("error:0200006E:rsa routines::data too large for key size");
    em[1] = 2;
    const ps = size - 3 - data.length;
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
    em.set(data, 3 + ps);
  } else if (padding === 4) {
    const hashName = digestName(input.oaepHash ?? "sha1");
    const lhash = crypto_hash2(hashName, new Uint8Array(0))!;
    const hl = lhash.length;
    if (data.length > size - 2 * hl - 2) throw new Error("error:0200006E:rsa routines::data too large for key size");
    const db = new Uint8Array(size - hl - 1);
    db.set(lhash, 0);
    db[db.length - data.length - 1] = 1;
    db.set(data, db.length - data.length);
    const seed = crypto_random_bytes(hl);
    const dbMask = mgf1(hashName, seed, db.length);
    for (let j = 0; j < db.length; j++) db[j] = db[j] ^ dbMask[j];
    const seedMask = mgf1(hashName, db, hl);
    for (let j = 0; j < hl; j++) em[1 + j] = seed[j] ^ seedMask[j];
    em.set(db, 1 + hl);
  } else {
    throw new Error("xisto: publicEncrypt supports RSA_PKCS1_PADDING and RSA_PKCS1_OAEP_PADDING");
  }
  return Buffer.from(fromBig(modPow(toBig(em), e, n), size));
}
