// passport-jwt: the JWT strategy's options (where the token is, the key that checks it, the claims it must have)
// and its check, as passport runs it: a token that is missing or does not pass fails the strategy (401), one that
// passes gives its payload to the strategy's validate(). HS256 takes the secret, RS256 a public key in PEM
// (or a key provider such as jwks-rsa's passportJwtSecret). @nestjs/passport's PassportStrategy uses it.
import { ClassInfo } from "xisto:reflect";
import { Request } from "express";

declare function verifyToken(token: string, key: string, algorithms: string, issuer: string, audience: string, ignoreExpiration: boolean, clockTolerance: int): string;

function described(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {};
}

/**
 * The JWT strategy, as PassportStrategy(Strategy, "name") names it ("jwt" when no name is given).
 * Node: passport-jwt's Strategy class is built by passport; here it only names the kind of strategy.
 */
@described()
export class Strategy {
  static readonly defaultName: string = "jwt";
}

/**
 * Gives the key a token is checked with, as in Node: a function (request, rawJwtToken, done) that calls
 * done(err) or done(null, key). jwks-rsa's passportJwtSecret() is one; done(null, false) means "no key" (the
 * strategy fails with "secret or public key must be provided", as jsonwebtoken does).
 */
export type SecretOrKeyProvider = (request: Request, rawJwtToken: string, done: (err: any, secretOrKey?: any) => void) => void;

/** jsonwebtoken's verify options passport-jwt passes on (clockTolerance, in seconds). */
export interface VerifyOptions {
  clockTolerance?: int;
}

/** Where the token comes from. */
export type JwtFromRequestFunction = (req: Request) => string | null;

/**
 * Options of the JWT strategy.
 * @example { jwtFromRequest: ExtractJwt.fromAuthHeaderAsBearerToken(), secretOrKey: "secret", ignoreExpiration: false }
 */
export interface StrategyOptions {
  jwtFromRequest: (req: Request) => string | null;
  secretOrKey?: string;
  secretOrKeyProvider?: SecretOrKeyProvider;
  issuer?: string;
  audience?: string;
  algorithms?: string[];
  ignoreExpiration?: boolean;
  passReqToCallback?: boolean;
  jsonWebTokenOptions?: VerifyOptions;
}

/** Ways to read the token from a request. */
export class ExtractJwt {
  /** "Authorization: Bearer <token>". */
  static fromAuthHeaderAsBearerToken(): (req: Request) => string | null {
    return ExtractJwt.fromAuthHeaderWithScheme("bearer");
  }
  /** "Authorization: <scheme> <token>", the scheme matched without case. */
  static fromAuthHeaderWithScheme(scheme: string): (req: Request) => string | null {
    return (req: Request): string | null => {
      const h = req.header("authorization");
      if (h === null) {
        return null;
      }
      const m = h.trim();
      const space = m.indexOf(" ");
      if (space < 0 || m.slice(0, space).toLowerCase() !== scheme.toLowerCase()) {
        return null;
      }
      return m.slice(space + 1, m.length).trim();
    };
  }
  /** A field of the JSON body ({ "refreshToken": "..." }). */
  static fromBodyField(field: string): (req: Request) => string | null {
    return (req: Request): string | null => {
      try {
        const body: any = JSON.parse(req.body);
        const v: any = body === null || typeof body !== "object" ? null : body[field];
        return typeof v === "string" ? (v as string) : null;
      } catch (e) {
        return null;
      }
    };
  }
  /** A header holding the token itself. */
  static fromHeader(name: string): (req: Request) => string | null {
    return (req: Request): string | null => req.header(name);
  }
  /** A query string parameter. */
  static fromUrlQueryParameter(name: string): (req: Request) => string | null {
    return (req: Request): string | null => req.query(name);
  }
}

/** What checking a request gave: the payload (JSON text) or why it failed. */
export class JwtCheck {
  ok: boolean = false;
  payload: string = "";
  message: string = "";
}

function failed(message: string): JwtCheck {
  const c = new JwtCheck();
  c.message = message;
  return c;
}

/**
 * Checks the request's token as passport-jwt does: missing ("No auth token"), no key, a bad signature, an
 * algorithm not allowed, expired, a wrong issuer or audience all fail; a token that passes gives its payload.
 */
export function checkRequest(options: StrategyOptions, req: Request): JwtCheck {
  const token = options.jwtFromRequest(req);
  if (token === null) {
    return failed("No auth token");
  }
  let key = options.secretOrKey ?? "";
  const provider = options.secretOrKeyProvider;
  if (provider !== null) {
    let got: string | null = null;
    let why: string | null = null;
    provider(req, token, (err: any, secretOrKey?: any): void => {
      if (err !== null) {
        why = err instanceof Error ? err.message : String(err);
      } else if (typeof secretOrKey === "string") {
        got = secretOrKey;
      } else if (Buffer.isBuffer(secretOrKey)) {
        got = (secretOrKey as Buffer).toString();
      }
    });
    if (why !== null) {
      return failed(why);
    }
    if (got === null) {
      return failed("secret or public key must be provided");
    }
    key = got;
  }
  const algs = options.algorithms;
  const tolerance = options.jsonWebTokenOptions;
  let clock: int = 0;
  if (tolerance !== null) {
    clock = tolerance.clockTolerance ?? 0;
  }
  try {
    const payload = verifyToken(token, key, algs === null ? "" : algs.join(","), options.issuer ?? "", options.audience ?? "", options.ignoreExpiration ?? false, clock);
    const c = new JwtCheck();
    c.ok = true;
    c.payload = payload;
    return c;
  } catch (e) {
    return failed(e.message);
  }
}
