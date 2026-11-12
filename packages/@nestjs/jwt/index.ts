// @nestjs/jwt for xisto: JwtModule.register / registerAsync provide JwtService, which signs and verifies HS256 tokens
// as jsonwebtoken 9 does (iat, exp from expiresIn, nbf, iss/aud/sub; the same error names and messages).
// Compatibility: HMAC algorithms only (HS256, HS384, HS512); RS256 keys go through passport-jwt / jose.
import { DynamicModule, FactoryProvider, Inject, Injectable, Module, Optional, ValueProvider } from "@nestjs/common";
import { createHmac, timingSafeEqual } from "node:crypto";

export const JWT_MODULE_OPTIONS = "JWT_MODULE_OPTIONS";

/** sign() options: expiresIn / notBefore as seconds or a time span ("15m", "7d", "2 hours"). */
export class JwtSignOptions {
  secret?: string;
  privateKey?: string;
  algorithm?: string;
  expiresIn?: string | number;
  notBefore?: string | number;
  issuer?: string;
  audience?: string;
  subject?: string;
  jwtid?: string;
  keyid?: string;
  noTimestamp?: boolean;
}

/** verify() options. */
export class JwtVerifyOptions {
  secret?: string;
  publicKey?: string;
  algorithms?: string[];
  issuer?: string;
  audience?: string;
  subject?: string;
  ignoreExpiration?: boolean;
  clockTolerance?: number;
}

/** What JwtModule.register takes. */
export class JwtModuleOptions {
  secret?: string;
  publicKey?: string;
  privateKey?: string;
  global?: boolean;
  signOptions?: JwtSignOptions;
  verifyOptions?: JwtVerifyOptions;
}

/** JwtModule.registerAsync({ imports, inject, useFactory }) */
export interface JwtModuleAsyncOptions {
  imports?: unknown[];
  inject?: unknown[];
  global?: boolean;
  useFactory: (...args: any[]) => JwtModuleOptions;
}

/** jsonwebtoken's errors (name and message as the npm package). */
export class JsonWebTokenError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "JsonWebTokenError";
  }
}
export class TokenExpiredError extends Error {
  expiredAt: Date;
  constructor(message: string, expiredAt: Date) {
    super(message);
    this.name = "TokenExpiredError";
    this.expiredAt = expiredAt;
  }
}
export class NotBeforeError extends Error {
  date: Date;
  constructor(message: string, date: Date) {
    super(message);
    this.name = "NotBeforeError";
    this.date = date;
  }
}

// vercel/ms: "15m", "7d", "2 hours", "100" (ms) -> milliseconds; NaN when not understood
function msOf(text: string): number {
  const m = /^(-?(?:\d+)?\.?\d+) *(milliseconds?|msecs?|ms|seconds?|secs?|s|minutes?|mins?|m|hours?|hrs?|h|days?|d|weeks?|w|years?|yrs?|y)?$/i.exec(text.trim());
  if (m === null) return NaN;
  const n = parseFloat(m[1]);
  const unit = (m[2] ?? "ms").toLowerCase();
  if (unit.startsWith("y")) return n * 31557600000;
  if (unit.startsWith("w")) return n * 604800000;
  if (unit.startsWith("d")) return n * 86400000;
  if (unit.startsWith("h")) return n * 3600000;
  if (unit === "m" || unit.startsWith("min")) return n * 60000;
  if (unit === "s" || unit.startsWith("sec")) return n * 1000;
  return n;
}

// jsonwebtoken's timespan: a number is seconds, a string a span from now
function timespan(v: unknown, iat: number, what: string): number {
  if (typeof v === "number") return iat + (v as number);
  if (typeof v === "string") {
    const ms = msOf(v as string);
    if (Number.isNaN(ms)) throw new Error(`"${what}" should be a number of seconds or string representing a timespan`);
    return Math.floor(iat + ms / 1000);
  }
  throw new Error(`"${what}" should be a number of seconds or string representing a timespan`);
}

function b64url(text: string): string {
  return Buffer.from(text, "utf8").toString("base64url");
}

function hashOf(alg: string): string {
  if (alg === "HS256") return "sha256";
  if (alg === "HS384") return "sha384";
  if (alg === "HS512") return "sha512";
  throw new JsonWebTokenError("invalid algorithm");
}

/** Signs and verifies tokens with the module's secret (or the one given per call). */
@Injectable()
export class JwtService {
  private readonly options: JwtModuleOptions;
  constructor(@Optional() @Inject(JWT_MODULE_OPTIONS) options: JwtModuleOptions | null = null) {
    this.options = options ?? {};
  }

  /** A token for the payload (an object): iat added, exp from expiresIn, as jsonwebtoken. */
  sign(payload: unknown, options: JwtSignOptions = {}): string {
    const base = this.options.signOptions ?? {};
    const secret = options.secret ?? options.privateKey ?? this.options.secret ?? this.options.privateKey ?? "";
    if (secret.length === 0) throw new Error("secretOrPrivateKey must have a value");
    const alg = options.algorithm ?? base.algorithm ?? "HS256";
    const claims: Record<string, unknown> = {};
    if (typeof payload === "string") throw new Error("xisto: @nestjs/jwt signs object payloads");
    const given: Record<string, unknown> = JSON.parse(JSON.stringify(payload as any));
    for (const k of Object.keys(given)) claims[k] = given[k];
    const given_iat: any = claims["iat"] ?? null;
    const iat = typeof given_iat === "number" ? (given_iat as number) : Math.floor(Date.now() / 1000); // (jsonwebtoken: payload.iat || now)
    if (!(options.noTimestamp ?? base.noTimestamp ?? false) && !("iat" in claims)) claims["iat"] = iat;
    const nbf: unknown = options.notBefore ?? base.notBefore ?? null;
    if (nbf !== null) claims["nbf"] = timespan(nbf, iat, "notBefore");
    const exp: unknown = options.expiresIn ?? base.expiresIn ?? null;
    if (exp !== null) {
      if ("exp" in claims) throw new Error('Bad "options.expiresIn" option the payload already has an "exp" property.');
      claims["exp"] = timespan(exp, iat, "expiresIn");
    }
    const aud = options.audience ?? base.audience ?? null;
    if (aud !== null) {
      if ("aud" in claims) throw new Error('Bad "options.audience" option. The payload already has an "aud" property.');
      claims["aud"] = aud;
    }
    const iss = options.issuer ?? base.issuer ?? null;
    if (iss !== null) {
      if ("iss" in claims) throw new Error('Bad "options.issuer" option. The payload already has an "iss" property.');
      claims["iss"] = iss;
    }
    const sub = options.subject ?? base.subject ?? null;
    if (sub !== null) {
      if ("sub" in claims) throw new Error('Bad "options.subject" option. The payload already has an "sub" property.');
      claims["sub"] = sub;
    }
    const jti = options.jwtid ?? base.jwtid ?? null;
    if (jti !== null) {
      if ("jti" in claims) throw new Error('Bad "options.jwtid" option. The payload already has an "jti" property.');
      claims["jti"] = jti;
    }
    const kid = options.keyid ?? base.keyid ?? null;
    const header = kid === null ? `{"alg":"${alg}","typ":"JWT"}` : `{"alg":"${alg}","typ":"JWT","kid":${JSON.stringify(kid)}}`;
    const input = b64url(header) + "." + b64url(JSON.stringify(claims));
    return input + "." + createHmac(hashOf(alg), secret).update(input).digest("base64url");
  }

  async signAsync(payload: unknown, options: JwtSignOptions = {}): Promise<string> {
    return this.sign(payload, options);
  }

  /** The payload of a valid token (signature, exp, nbf, iss, aud checked); throws jsonwebtoken's errors otherwise. */
  verify(token: string, options: JwtVerifyOptions = {}): any {
    const base = this.options.verifyOptions ?? {};
    const secret = options.secret ?? options.publicKey ?? this.options.secret ?? this.options.publicKey ?? "";
    const parts = token.split(".");
    if (parts.length !== 3) throw new JsonWebTokenError("jwt malformed");
    let header: any = null;
    let payload: any = null;
    try {
      header = JSON.parse(Buffer.from(parts[0], "base64url").toString("utf8"));
      payload = JSON.parse(Buffer.from(parts[1], "base64url").toString("utf8"));
    } catch (e) {
      throw new JsonWebTokenError("invalid token");
    }
    if (parts[2].length === 0) throw new JsonWebTokenError("jwt signature is required");
    if (secret.length === 0) throw new JsonWebTokenError("secret or public key must be provided");
    const alg = String(header.alg);
    const allowed = options.algorithms ?? base.algorithms ?? ["HS256", "HS384", "HS512"];
    if (!allowed.includes(alg)) throw new JsonWebTokenError("invalid algorithm");
    const expected = Buffer.from(createHmac(hashOf(alg), secret).update(parts[0] + "." + parts[1]).digest("base64url"));
    const got = Buffer.from(parts[2]);
    if (expected.length !== got.length || !timingSafeEqual(expected, got)) throw new JsonWebTokenError("invalid signature");
    const now = Math.floor(Date.now() / 1000);
    const tol = options.clockTolerance ?? base.clockTolerance ?? 0;
    const nbf: any = payload.nbf;
    if (typeof nbf === "number" && nbf > now + tol) throw new NotBeforeError("jwt not active", new Date(nbf * 1000));
    const exp: any = payload.exp;
    if (!(options.ignoreExpiration ?? base.ignoreExpiration ?? false) && typeof exp === "number" && now >= exp + tol)
      throw new TokenExpiredError("jwt expired", new Date(exp * 1000));
    const iss = options.issuer ?? base.issuer ?? null;
    if (iss !== null && payload.iss !== iss) throw new JsonWebTokenError(`jwt issuer invalid. expected: ${iss}`);
    const aud = options.audience ?? base.audience ?? null;
    if (aud !== null && payload.aud !== aud) throw new JsonWebTokenError(`jwt audience invalid. expected: ${aud}`);
    const sub = options.subject ?? base.subject ?? null;
    if (sub !== null && payload.sub !== sub) throw new JsonWebTokenError(`jwt subject invalid. expected: ${sub}`);
    return payload;
  }

  async verifyAsync(token: string, options: JwtVerifyOptions = {}): Promise<any> {
    return this.verify(token, options);
  }

  /** The payload without checking anything (null when the token is not one). */
  decode(token: string): any {
    const parts = token.split(".");
    if (parts.length !== 3) return null;
    try {
      const payload: any = JSON.parse(Buffer.from(parts[1], "base64url").toString("utf8"));
      return payload;
    } catch (e) {
      return null;
    }
  }
}

@Module({ providers: [JwtService], exports: [JwtService] })
export class JwtModule {
  /** Provides JwtService with these options. */
  static register(options: JwtModuleOptions = {}): DynamicModule {
    const p: ValueProvider = { provide: JWT_MODULE_OPTIONS, useValue: options };
    return { module: JwtModule, providers: [p, JwtService], exports: [JwtService], global: options.global ?? false };
  }
  /** The options from a factory, with the providers it injects (ConfigService). */
  static registerAsync(options: JwtModuleAsyncOptions): DynamicModule {
    const p: FactoryProvider = { provide: JWT_MODULE_OPTIONS, useFactory: (...args: any[]): unknown => options.useFactory(...args), inject: options.inject ?? [] };
    return { module: JwtModule, imports: options.imports ?? [], providers: [p, JwtService], exports: [JwtService], global: options.global ?? false };
  }
}
