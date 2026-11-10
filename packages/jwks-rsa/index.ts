// jwks-rsa: the signing keys of a JSON Web Key Set served over HTTP (an identity provider's /certs), fetched
// once and kept (cache), turned into PEM public keys; passportJwtSecret() gives passport-jwt the key of each
// token's "kid".
import { Request } from "express";
import { SecretOrKeyProvider } from "passport-jwt";

declare function jwkToPem(n: string, e: string): string;
declare function headerField(token: string, name: string): string;

/** Options of JwksClient and passportJwtSecret (cache and rate limits are accepted; keys are kept once fetched). */
export interface ClientOptions {
  jwksUri: string;
  cache?: boolean;
  cacheMaxEntries?: int;
  cacheMaxAge?: int;
  rateLimit?: boolean;
  jwksRequestsPerMinute?: int;
  timeout?: int;
}
export type ExpressJwtOptions = ClientOptions;

/** One key of the set, as the JWKS gives it. */
export class JsonWebKey {
  kty: string = "";
  kid?: string;
  use?: string;
  alg?: string;
  n?: string;
  e?: string;
}
class JsonWebKeySet {
  keys: JsonWebKey[] = [];
}

/** A key that signs tokens: its id, its algorithm and its public key in PEM. */
export class SigningKey {
  readonly kid: string;
  readonly alg: string;
  readonly publicKey: string;
  readonly rsaPublicKey: string;
  constructor(kid: string, alg: string, publicKey: string) {
    this.kid = kid;
    this.alg = alg;
    this.publicKey = publicKey;
    this.rsaPublicKey = publicKey;
  }
  getPublicKey(): string {
    return this.publicKey;
  }
}

/** jwks-rsa's errors keep their names: SigningKeyNotFoundError, JwksError. */
export class SigningKeyNotFoundError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "SigningKeyNotFoundError";
  }
}
export class JwksError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "JwksError";
  }
}

/**
 * Reads a JSON Web Key Set.
 * @example const key = new JwksClient({ jwksUri: "https://idp/certs" }).getSigningKey(kid).getPublicKey()
 * Node: getSigningKeys() and getSigningKey() return promises; here they return the value (await works on both).
 */
export class JwksClient {
  private readonly options: ClientOptions;
  private readonly byKid: Map<string, SigningKey> = new Map<string, SigningKey>();
  constructor(options: ClientOptions) {
    this.options = options;
  }
  /** The RSA signing keys of the set (use "sig" or none), fetched from the JWKS. */
  getSigningKeys(): SigningKey[] {
    return fetchKeys(this.options.jwksUri, this.options.timeout ?? 30000);
  }
  /**
   * The key with this kid (or the only key, when the token names none). With the cache on, a kid found once is
   * kept; a kid not seen yet fetches the set again.
   */
  getSigningKey(kid: string = ""): SigningKey {
    const cache = this.options.cache ?? true;
    const have = this.byKid.get(kid);
    if (cache && have !== null) {
      return have;
    }
    const keys = this.getSigningKeys();
    let found: SigningKey | null = null;
    if (kid.length === 0 && keys.length === 1) {
      found = keys[0];
    }
    for (const k of keys) {
      if (k.kid === kid) {
        found = k;
      }
    }
    if (found === null) {
      throw new SigningKeyNotFoundError(`Unable to find a signing key that matches '${kid}'`);
    }
    if (cache) {
      this.byKid.set(kid, found);
    }
    return found;
  }
}

function fetchKeys(uri: string, timeout: int): SigningKey[] {
  const text = getText(uri, timeout);
  let set = new JsonWebKeySet();
  try {
    set = JSON.parse(text);
  } catch (e) {
    throw new JwksError(e.message);
  }
  const out: SigningKey[] = [];
  for (const k of set.keys) {
    const use = k.use ?? "sig";
    const n = k.n ?? "";
    const e = k.e ?? "";
    if (use !== "sig" || k.kty !== "RSA" || n.length === 0 || e.length === 0) {
      continue;
    }
    out.push(new SigningKey(k.kid ?? "", k.alg ?? "RS256", jwkToPem(n, e)));
  }
  if (out.length === 0) {
    throw new JwksError("The JWKS endpoint did not contain any signing keys");
  }
  return out;
}

async function getText(uri: string, timeout: int): Promise<string> {
  const r: Response = await fetch(uri, { signal: AbortSignal.timeout(timeout) });
  if (r.status < 200 || r.status >= 300) {
    const body = await r.text();
    throw new JwksError(body.length > 0 ? body : `Http Error ${r.status}`);
  }
  return await r.text();
}

class JwksKeyProvider extends SecretOrKeyProvider {
  private readonly client: JwksClient;
  constructor(options: ExpressJwtOptions) {
    super();
    this.client = new JwksClient(options);
  }
  override keyFor(req: Request, rawJwt: string): string {
    const alg = headerField(rawJwt, "alg");
    if (alg !== "RS256" && alg !== "RS384" && alg !== "RS512" && alg !== "PS256" && alg !== "PS384" && alg !== "PS512" && alg !== "ES256" && alg !== "ES384" && alg !== "ES512") {
      throw new Error("secret or public key must be provided");
    }
    return this.client.getSigningKey(headerField(rawJwt, "kid")).getPublicKey();
  }
}

/**
 * The key provider for passport-jwt's secretOrKeyProvider: each token's key from the JWKS, by its "kid".
 * @example secretOrKeyProvider: passportJwtSecret({ cache: true, rateLimit: true, jwksUri: "https://idp/certs" })
 */
export function passportJwtSecret(options: ExpressJwtOptions): SecretOrKeyProvider {
  return new JwksKeyProvider(options);
}
