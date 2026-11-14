// xisto:https (node:https): createServer({ key, cert }, listener), request() and get() over TLS; the rest is
// node:http's (xisto:http). Importing it links TLS into the program (AWS-LC by default, or BearSSL).
// Differences from Node (docs/docs/guides/node-compatibility.md): the same as node:http's; key and cert are PEM text
// (or Buffers of it); ca, passphrase, SNI and client certificates are not supported yet.
import { HttpServer, RequestListener, ClientRequest, RequestOptions, ResponseListener, _setup, _toUrl, _method } from "xisto:http";

declare function https_bind(s: Server, port: int, cert: string, key: string): int;

/** The options of createServer(): the certificate and its private key, PEM. */
export interface ServerOptions {
  key: string | Uint8Array;
  cert: string | Uint8Array;
}

function pem(v: string | Uint8Array): string {
  return typeof v === "string" ? (v as string) : new TextDecoder().decode(v as Uint8Array);
}

/** Node's https.Server: an http server whose connections are TLS. */
export class HttpsServer extends HttpServer {
  private cert: string;
  private key: string;
  constructor(options: ServerOptions, listener: RequestListener | null = null) {
    super(listener);
    this.cert = pem(options.cert);
    this.key = pem(options.key);
  }
  protected bind(native: Server, port: int): int {
    return https_bind(native, port, this.cert, this.key);
  }
}

/** A TLS server that calls the listener for each request. */
export function createServer(options: ServerOptions, listener: RequestListener | null = null): HttpsServer {
  return new HttpsServer(options, listener);
}

/** A request over TLS. */
export class HttpsClientRequest extends ClientRequest {
  protected expected(): string {
    return "https:";
  }
  protected async fetchRest(rest: string, withBody: boolean, h: Record<string, string>, body: string): Promise<Response> {
    if (!withBody) return await fetch("https://" + rest, { method: this.method, headers: h });
    return await fetch("https://" + rest, { method: this.method, headers: h, body: body });
  }
}

/** Makes an https request: call end() (with a body) to send it. */
export function request(target: string | RequestOptions, callback: ResponseListener | null = null): ClientRequest {
  return _setup(new HttpsClientRequest(_toUrl(target, "https:"), _method(target)), target, callback);
}

/** An https GET request, sent at once. */
export function get(target: string | RequestOptions, callback: ResponseListener | null = null): ClientRequest {
  const req = request(target, callback);
  req.end();
  return req;
}
