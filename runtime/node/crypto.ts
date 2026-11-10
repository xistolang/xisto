// xisto:crypto (node:crypto): randomUUID, createHash and createHmac over md5, sha1, sha256 and sha512.
// Data is text (UTF-8, Node's default for update(string)); a digest is text in an encoding (hex, base64,
// base64url, latin1): digest() with no encoding, which gives a Buffer in Node, is not here.

declare function crypto_uuid(): string;
declare function crypto_supported(algorithm: string): boolean;
declare function crypto_digest(algorithm: string, data: string, encoding: string): string;
declare function crypto_hmac(algorithm: string, key: string, data: string, encoding: string): string;

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
  /** Adds data (UTF-8 text) to what is hashed. */
  update(data: string): Hash {
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
  /** Adds data (UTF-8 text) to what is authenticated. */
  update(data: string): Hmac {
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
