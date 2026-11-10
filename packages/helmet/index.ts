// helmet for xisto: the security headers helmet 7 sets by default, as an Express middleware.
import { NextFunction, Request, RequestHandler, Response } from "express";

/** Options (accepted; the defaults are always used, with contentSecurityPolicy: false honored). */
export class HelmetOptions {
  contentSecurityPolicy?: boolean;
  crossOriginEmbedderPolicy?: boolean;
}

const CSP =
  "default-src 'self';base-uri 'self';font-src 'self' https: data:;form-action 'self';frame-ancestors 'self';img-src 'self' data:;object-src 'none';script-src 'self';script-src-attr 'none';style-src 'self' https: 'unsafe-inline';upgrade-insecure-requests";

/**
 * The middleware that sets helmet's default headers on every response.
 * @example app.use(helmet())
 */
export default function helmet(options: HelmetOptions = {}): RequestHandler {
  const csp = options.contentSecurityPolicy;
  const withCsp = csp === null || csp === true;
  return (req: Request, res: Response, next: NextFunction): void => {
    if (withCsp) {
      res.setHeader("Content-Security-Policy", CSP);
    }
    res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
    res.setHeader("Cross-Origin-Resource-Policy", "same-origin");
    res.setHeader("Origin-Agent-Cluster", "?1");
    res.setHeader("Referrer-Policy", "no-referrer");
    res.setHeader("Strict-Transport-Security", "max-age=15552000; includeSubDomains");
    res.setHeader("X-Content-Type-Options", "nosniff");
    res.setHeader("X-DNS-Prefetch-Control", "off");
    res.setHeader("X-Download-Options", "noopen");
    res.setHeader("X-Frame-Options", "SAMEORIGIN");
    res.setHeader("X-Permitted-Cross-Domain-Policies", "none");
    res.setHeader("X-XSS-Protection", "0");
    next();
  };
}
