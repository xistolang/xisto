// compression for xisto: the middleware is accepted and answers uncompressed (a valid answer for every client;
// gzip on the server side is not in xisto's runtime yet).
import { NextFunction, Request, RequestHandler, Response } from "express";

/** Options (accepted). */
export class CompressionOptions {
  threshold?: int;
  level?: int;
}

// the media types compression's default filter compresses (compressible in mime-db)
function compressible(contentType: string): boolean {
  const t = contentType.split(";")[0].trim().toLowerCase();
  return t.startsWith("text/") || t.endsWith("json") || t.endsWith("+json") || t.endsWith("xml") || t.endsWith("+xml") || t === "application/javascript" || t === "image/svg+xml";
}

/**
 * The compression middleware. Compatibility: responses go out uncompressed; Vary: Accept-Encoding is set on the
 * responses compression would compress (a compressible Content-Type, no Cache-Control: no-transform), as it does.
 * @example app.use(compression())
 */
export default function compression(options: CompressionOptions = {}): RequestHandler {
  return (req: Request, res: Response, next: NextFunction): void => {
    res.onHeaders((r: Response): void => {
      const ct = r.getHeader("Content-Type");
      const cc = r.getHeader("Cache-Control");
      if (ct === null || !compressible(ct) || (cc !== null && cc.includes("no-transform"))) {
        return;
      }
      const vary = r.getHeader("Vary");
      if (vary === null) {
        r.setHeader("Vary", "Accept-Encoding");
      } else if (!vary.toLowerCase().includes("accept-encoding")) {
        r.setHeader("Vary", vary + ", Accept-Encoding");
      }
    });
    next();
  };
}
