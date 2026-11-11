// jose (v5), the RS256 part: keys from PEM (importPKCS8, importSPKI) or generated (generateKeyPair), exportJWK,
// SignJWT and jwtVerify with jose's claim checks, error classes, codes and messages. RSA (signing, verification,
// key generation) is native/rsa.c; a KeyLike keeps its key as PEM.
//
// Payloads and protected headers are Record<string, unknown> (jose: JWTPayload and JWTHeaderParameters, interfaces
// with an index signature): the claims keep the order they were set in, as in Node.
// Node: only RS256 is here (sign and verify); other algorithms fail with JOSENotSupported. Keys are KeyLike objects
// (Node: KeyObject); Uint8Array secrets, JWE, JWKS and the `errors` namespace are not here: the error classes are
// exported by name.
import { TypeError } from "xisto:errors";

declare function rsaGenerate(bits: int): string;
declare function rsaKeyBits(pem: string, kind: string): int;
declare function rsaPublicPem(pem: string): string;
declare function rsaJwk(pem: string): string;
declare function rsaSign(pem: string, data: string): string;
declare function rsaVerify(pem: string, data: string, signature: string): boolean;
declare function b64urlEncode(text: string): string;
declare function b64urlDecode(text: string): string | null;

/** The claims of a JWT: iss, sub, aud, jti, nbf, exp, iat and any other member. */
export type JWTPayload = Record<string, unknown>;
/** A JWS protected header: alg, kid, typ and any other member. */
export type JWTHeaderParameters = Record<string, unknown>;
export type JWSHeaderParameters = Record<string, unknown>;
export type CompactJWSHeaderParameters = Record<string, unknown>;

/** A key (Node: a KeyObject): type is "private" or "public". */
export class KeyLike {
  readonly type: string;
  readonly asymmetricKeyType: string = "rsa";
  /** @internal the key as PEM (PKCS#8 for a private key, SPKI for a public one) */
  readonly pem: string;
  constructor(type: string, pem: string) {
    this.type = type;
    this.pem = pem;
  }
}

/** A JSON Web Key; exportJWK fills kty, n, e (and d, p, q, dp, dq, qi for a private key). */
export interface JWK {
  kty?: string;
  n?: string;
  e?: string;
  d?: string;
  p?: string;
  q?: string;
  dp?: string;
  dq?: string;
  qi?: string;
  kid?: string;
  use?: string;
  alg?: string;
  key_ops?: string[];
  x5c?: string[];
}

export interface GenerateKeyPairOptions {
  modulusLength?: int;
  extractable?: boolean;
}

export interface GenerateKeyPairResult {
  privateKey: KeyLike;
  publicKey: KeyLike;
}

export interface JWTVerifyOptions {
  issuer?: string | string[];
  audience?: string | string[];
  subject?: string;
  algorithms?: string[];
  /** seconds, or a time span such as "30s" */
  clockTolerance?: number | string;
  typ?: string;
  requiredClaims?: string[];
}

export interface JWTVerifyResult {
  payload: JWTPayload;
  protectedHeader: CompactJWSHeaderParameters;
}

// ---- errors: jose's classes, codes and messages ----

/** The base of jose's errors; code is "ERR_JOSE_GENERIC" or the subclass's. */
export class JOSEError extends Error {
  code: string;
  constructor(message: string, code: string = "ERR_JOSE_GENERIC", name: string = "JOSEError") {
    super(message);
    this.name = name;
    this.code = code;
  }
}
/** A JWT claim that is missing, malformed or does not match (claim, reason "missing" | "check_failed" | "invalid"). */
export class JWTClaimValidationFailed extends JOSEError {
  claim: string;
  reason: string;
  payload: JWTPayload;
  constructor(message: string, payload: JWTPayload, claim: string = "unspecified", reason: string = "unspecified") {
    super(message, "ERR_JWT_CLAIM_VALIDATION_FAILED", "JWTClaimValidationFailed");
    this.claim = claim;
    this.reason = reason;
    this.payload = payload;
  }
}
/** An "exp" in the past. */
export class JWTExpired extends JOSEError {
  claim: string;
  reason: string;
  payload: JWTPayload;
  constructor(message: string, payload: JWTPayload, claim: string = "unspecified", reason: string = "unspecified") {
    super(message, "ERR_JWT_EXPIRED", "JWTExpired");
    this.claim = claim;
    this.reason = reason;
    this.payload = payload;
  }
}
export class JOSEAlgNotAllowed extends JOSEError {
  constructor(message: string) {
    super(message, "ERR_JOSE_ALG_NOT_ALLOWED", "JOSEAlgNotAllowed");
  }
}
export class JOSENotSupported extends JOSEError {
  constructor(message: string) {
    super(message, "ERR_JOSE_NOT_SUPPORTED", "JOSENotSupported");
  }
}
export class JWSInvalid extends JOSEError {
  constructor(message: string) {
    super(message, "ERR_JWS_INVALID", "JWSInvalid");
  }
}
export class JWTInvalid extends JOSEError {
  constructor(message: string) {
    super(message, "ERR_JWT_INVALID", "JWTInvalid");
  }
}
export class JWSSignatureVerificationFailed extends JOSEError {
  constructor(message: string = "signature verification failed") {
    super(message, "ERR_JWS_SIGNATURE_VERIFICATION_FAILED", "JWSSignatureVerificationFailed");
  }
}

// ---- keys ----

function checkAlg(alg: string): void {
  if (alg !== "RS256") {
    throw new JOSENotSupported(`alg ${alg} is not supported by xisto's jose (RS256 only)`);
  }
}

/** A private key from PKCS#8 PEM ("-----BEGIN PRIVATE KEY-----"). */
export async function importPKCS8(pkcs8: string, alg: string): Promise<KeyLike> {
  if (!pkcs8.startsWith("-----BEGIN PRIVATE KEY-----")) {
    throw new TypeError('"pkcs8" must be PKCS#8 formatted string');
  }
  checkAlg(alg);
  const bits = rsaKeyBits(pkcs8, "pkcs8");
  if (bits === 0) {
    throw new TypeError("Invalid key data: not an RSA private key in PKCS#8");
  }
  if (bits < 2048) {
    throw new TypeError(`${alg} requires key modulusLength to be 2048 bits or larger`);
  }
  return new KeyLike("private", pkcs8);
}

/** A public key from SPKI PEM ("-----BEGIN PUBLIC KEY-----"). */
export async function importSPKI(spki: string, alg: string): Promise<KeyLike> {
  if (!spki.startsWith("-----BEGIN PUBLIC KEY-----")) {
    throw new TypeError('"spki" must be SPKI formatted string');
  }
  checkAlg(alg);
  const bits = rsaKeyBits(spki, "spki");
  if (bits === 0) {
    throw new TypeError("Invalid key data: not an RSA public key in SPKI");
  }
  if (bits < 2048) {
    throw new TypeError(`${alg} requires key modulusLength to be 2048 bits or larger`);
  }
  return new KeyLike("public", spki);
}

/** A new RSA key pair (e = 65537) for RS256; modulusLength 2048 (default) to 4096. */
export async function generateKeyPair(alg: string, options: GenerateKeyPairOptions = {}): Promise<GenerateKeyPairResult> {
  checkAlg(alg);
  const bits = options.modulusLength ?? 2048;
  if (bits < 2048 || bits > 4096 || bits % 128 !== 0) {
    throw new JOSENotSupported("Invalid or unsupported modulusLength option provided, 2048 bits or larger keys must be used");
  }
  const pem = rsaGenerate(bits);
  return { privateKey: new KeyLike("private", pem), publicKey: new KeyLike("public", rsaPublicPem(pem)) };
}

/** The key as a JWK: kty, n, e (and d, p, q, dp, dq, qi for a private key), as Node's KeyObject exports it. */
export async function exportJWK(key: KeyLike): Promise<JWK> {
  const parts = rsaJwk(key.pem).split(".");
  if (parts.length === 8) {
    const jwk: JWK = { kty: "RSA", n: parts[0], e: parts[1], d: parts[2], p: parts[3], q: parts[4], dp: parts[5], dq: parts[6], qi: parts[7] };
    return jwk;
  }
  const jwk: JWK = { kty: "RSA", n: parts[0], e: parts[1] };
  return jwk;
}

/** The public key as SPKI PEM. */
export async function exportSPKI(key: KeyLike): Promise<string> {
  return rsaPublicPem(key.pem);
}

/** A private key as PKCS#8 PEM. */
export async function exportPKCS8(key: KeyLike): Promise<string> {
  if (key.type !== "private") {
    throw new TypeError("key is not a private key");
  }
  return key.pem;
}

// ---- time ----

function epochNow(): number {
  return Math.floor(Date.now() / 1000);
}

/** Seconds of a time span: "30s", "10 minutes", "2h", "1.5 days", "-5m", "1 week ago" (jose's secs). */
function secs(text: string): number {
  const matched = /^(\+|\-)? ?(\d+|\d+\.\d+) ?(seconds?|secs?|s|minutes?|mins?|m|hours?|hrs?|h|days?|d|weeks?|w|years?|yrs?|y)(?: (ago|from now))?$/i.exec(text);
  if (matched === null || ((matched[4] ?? "") !== "" && (matched[1] ?? "") !== "")) {
    throw new TypeError("Invalid time period format");
  }
  const value = parseFloat(matched[2]);
  const unit = matched[3].toLowerCase();
  let n = 0;
  if (unit === "sec" || unit === "secs" || unit === "second" || unit === "seconds" || unit === "s") {
    n = Math.round(value);
  } else if (unit === "minute" || unit === "minutes" || unit === "min" || unit === "mins" || unit === "m") {
    n = Math.round(value * 60);
  } else if (unit === "hour" || unit === "hours" || unit === "hr" || unit === "hrs" || unit === "h") {
    n = Math.round(value * 3600);
  } else if (unit === "day" || unit === "days" || unit === "d") {
    n = Math.round(value * 86400);
  } else if (unit === "week" || unit === "weeks" || unit === "w") {
    n = Math.round(value * 604800);
  } else {
    n = Math.round(value * 31557600);
  }
  if (matched[1] === "-" || matched[4] === "ago") {
    return -n;
  }
  return n;
}

function finite(label: string, input: number): number {
  if (!Number.isFinite(input)) {
    throw new TypeError(`Invalid ${label} input`);
  }
  return input;
}

// ---- SignJWT ----

/**
 * A JWT being built: claims, then the protected header, then sign(privateKey).
 * @example await new SignJWT({ tid }).setProtectedHeader({ alg: "RS256", kid }).setIssuer(iss).setAudience(aud)
 *   .setSubject(sub).setIssuedAt().setExpirationTime("2h").sign(privateKey)
 */
export class SignJWT {
  private readonly payload: Record<string, unknown> = {};
  private header: Record<string, unknown> | null = null;

  constructor(payload: JWTPayload = {}) {
    for (const k of Object.keys(payload)) {
      this.payload[k] = payload[k];
    }
  }

  setProtectedHeader(header: JWTHeaderParameters): SignJWT {
    if (this.header !== null) {
      throw new TypeError("setProtectedHeader can only be called once");
    }
    this.header = header;
    return this;
  }
  setIssuer(issuer: string): SignJWT {
    this.payload["iss"] = issuer;
    return this;
  }
  setSubject(subject: string): SignJWT {
    this.payload["sub"] = subject;
    return this;
  }
  setAudience(audience: string | string[]): SignJWT {
    this.payload["aud"] = audience;
    return this;
  }
  setJti(jwtId: string): SignJWT {
    this.payload["jti"] = jwtId;
    return this;
  }
  /** now, a NumericDate (seconds), or a time span from now ("10s") */
  setIssuedAt(input: number | string | null = null): SignJWT {
    if (input === null) {
      this.payload["iat"] = epochNow();
    } else if (typeof input === "string") {
      this.payload["iat"] = finite("setIssuedAt", epochNow() + secs(input));
    } else {
      this.payload["iat"] = finite("setIssuedAt", input);
    }
    return this;
  }
  /** a NumericDate (seconds), or a time span from now ("2h", "120s") */
  setExpirationTime(input: number | string): SignJWT {
    if (typeof input === "string") {
      this.payload["exp"] = epochNow() + secs(input);
    } else {
      this.payload["exp"] = finite("setExpirationTime", input);
    }
    return this;
  }
  /** a NumericDate (seconds), or a time span from now */
  setNotBefore(input: number | string): SignJWT {
    if (typeof input === "string") {
      this.payload["nbf"] = epochNow() + secs(input);
    } else {
      this.payload["nbf"] = finite("setNotBefore", input);
    }
    return this;
  }

  /** The compact JWS: header.payload.signature (base64url). */
  async sign(key: KeyLike): Promise<string> {
    const header = this.header;
    if (header === null) {
      throw new JWSInvalid("either setProtectedHeader or setUnprotectedHeader must be called before #sign()");
    }
    const alg = header["alg"];
    if (typeof alg !== "string" || alg.length === 0) {
      throw new JWSInvalid('JWS "alg" (Algorithm) Header Parameter missing or invalid');
    }
    checkAlg(alg);
    if (key.type !== "private") {
      throw new TypeError('KeyObject instances for asymmetric algorithm signing must be of type "private"');
    }
    const input = `${b64urlEncode(JSON.stringify(header))}.${b64urlEncode(JSON.stringify(this.payload))}`;
    return `${input}.${rsaSign(key.pem, input)}`;
  }
}

// ---- jwtVerify ----

function listOf(v: string | string[]): string[] {
  if (typeof v === "string") {
    return [v];
  }
  return v;
}

function audienceMatches(aud: unknown, wanted: string[]): boolean {
  if (typeof aud === "string") {
    return wanted.includes(aud);
  }
  if (Array.isArray(aud)) {
    for (const a of aud as unknown[]) {
      if (typeof a === "string" && wanted.includes(a)) {
        return true;
      }
    }
  }
  return false;
}

function normalizeTyp(value: string): string {
  const v = value.toLowerCase();
  return v.startsWith("application/") ? v.slice(12) : v;
}

function parseObject(text: string | null): Record<string, unknown> | null {
  if (text === null) {
    return null;
  }
  try {
    const v: unknown = JSON.parse(text);
    if (v === null || typeof v !== "object" || Array.isArray(v)) {
      return null;
    }
    return v as Record<string, unknown>;
  } catch (e) {
    return null;
  }
}

// jose's claim checks, in its order
function checkClaims(header: Record<string, unknown>, payload: JWTPayload, options: JWTVerifyOptions): void {
  const typ = options.typ;
  if (typ !== null && typ.length > 0) {
    const t = header["typ"];
    if (typeof t !== "string" || normalizeTyp(t) !== normalizeTyp(typ)) {
      throw new JWTClaimValidationFailed('unexpected "typ" JWT header value', payload, "typ", "check_failed");
    }
  }
  const presence: string[] = [];
  for (const c of options.requiredClaims ?? []) {
    presence.push(c);
  }
  if (options.audience !== null) {
    presence.push("aud");
  }
  if (options.subject !== null) {
    presence.push("sub");
  }
  if (options.issuer !== null) {
    presence.push("iss");
  }
  const seen: string[] = [];
  for (let i = presence.length - 1; i >= 0; i--) {
    const claim = presence[i];
    if (seen.includes(claim)) {
      continue;
    }
    seen.push(claim);
    if (!(claim in payload)) {
      throw new JWTClaimValidationFailed(`missing required "${claim}" claim`, payload, claim, "missing");
    }
  }
  const issuer = options.issuer;
  if (issuer !== null) {
    const iss = payload["iss"];
    if (typeof iss !== "string" || !listOf(issuer).includes(iss)) {
      throw new JWTClaimValidationFailed('unexpected "iss" claim value', payload, "iss", "check_failed");
    }
  }
  const subject = options.subject;
  if (subject !== null && subject.length > 0 && payload["sub"] !== subject) {
    throw new JWTClaimValidationFailed('unexpected "sub" claim value', payload, "sub", "check_failed");
  }
  const audience = options.audience;
  if (audience !== null && !audienceMatches(payload["aud"], listOf(audience))) {
    throw new JWTClaimValidationFailed('unexpected "aud" claim value', payload, "aud", "check_failed");
  }
  let tolerance = 0;
  const ct = options.clockTolerance;
  if (ct !== null) {
    tolerance = typeof ct === "string" ? secs(ct) : ct;
  }
  const now = epochNow();
  const iat = payload["iat"];
  if ("iat" in payload && typeof iat !== "number") {
    throw new JWTClaimValidationFailed('"iat" claim must be a number', payload, "iat", "invalid");
  }
  if ("nbf" in payload) {
    const nbf = payload["nbf"];
    if (typeof nbf !== "number") {
      throw new JWTClaimValidationFailed('"nbf" claim must be a number', payload, "nbf", "invalid");
    }
    if (nbf > now + tolerance) {
      throw new JWTClaimValidationFailed('"nbf" claim timestamp check failed', payload, "nbf", "check_failed");
    }
  }
  if ("exp" in payload) {
    const exp = payload["exp"];
    if (typeof exp !== "number") {
      throw new JWTClaimValidationFailed('"exp" claim must be a number', payload, "exp", "invalid");
    }
    if (exp <= now - tolerance) {
      throw new JWTExpired('"exp" claim timestamp check failed', payload, "exp", "check_failed");
    }
  }
}

/**
 * Checks a compact JWT: its form, the header's alg (against options.algorithms), the RS256 signature with the
 * key, then the claims (iss, sub, aud, typ, nbf, exp with options.clockTolerance). Throws jose's errors.
 * @example const { payload } = await jwtVerify(token, publicKey, { issuer, audience });
 */
export async function jwtVerify(jwt: string, key: KeyLike, options: JWTVerifyOptions = {}): Promise<JWTVerifyResult> {
  const parts = jwt.split(".");
  if (parts.length !== 3) {
    throw new JWSInvalid("Invalid Compact JWS");
  }
  const header = parseObject(b64urlDecode(parts[0]));
  if (header === null) {
    throw new JWSInvalid("JWS Protected Header is invalid");
  }
  const alg = header["alg"];
  if (typeof alg !== "string" || alg.length === 0) {
    throw new JWSInvalid('JWS "alg" (Algorithm) Header Parameter missing or invalid');
  }
  const algorithms = options.algorithms;
  if (algorithms !== null && !algorithms.includes(alg)) {
    throw new JOSEAlgNotAllowed('"alg" (Algorithm) Header Parameter value not allowed');
  }
  if (alg.startsWith("HS")) {
    throw new TypeError('KeyObject instances for symmetric algorithms must be of type "secret"');
  }
  if (alg !== "RS256") {
    throw new JOSENotSupported(`alg ${alg} is not supported either by JOSE or your javascript runtime`);
  }
  if (!rsaVerify(key.pem, `${parts[0]}.${parts[1]}`, parts[2])) {
    throw new JWSSignatureVerificationFailed();
  }
  const payload = parseObject(b64urlDecode(parts[1]));
  if (payload === null) {
    throw new JWTInvalid("JWT Claims Set must be a top-level JSON object");
  }
  checkClaims(header, payload, options);
  return { payload: payload, protectedHeader: header };
}

/** The protected header of a compact JWT, without checking it. */
export function decodeProtectedHeader(token: string): CompactJWSHeaderParameters {
  const header = parseObject(b64urlDecode(token.split(".")[0]));
  if (header === null) {
    throw new TypeError("Invalid Token or Protected Header formatting");
  }
  return header;
}

/** The claims of a compact JWT, without checking it. */
export function decodeJwt(jwt: string): JWTPayload {
  const parts = jwt.split(".");
  if (parts.length !== 3) {
    throw new JWTInvalid("Invalid JWT");
  }
  const payload = parseObject(b64urlDecode(parts[1]));
  if (payload === null) {
    throw new JWTInvalid("Invalid JWT Claims Set");
  }
  return payload;
}
