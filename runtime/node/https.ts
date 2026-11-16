// xisto:https (node:https): createServer({ key, cert }, listener), request() and get() over TLS; the rest is
// node:http's (xisto:http), with its connections wrapped in TLS. Importing it links TLS into the program (AWS-LC by
// default, or BearSSL). Differences from Node (docs/docs/guides/node-compatibility.md): the same as node:http's; key
// and cert are PEM text (or Buffers of it); ca adds to the CAs the whole program trusts; passphrase, SNI on the server
// and client certificates are not supported yet.
import { Server as HttpServer, HttpTransport, HttpSystemError, RequestListener, ClientRequest, RequestOptions, ResponseListener, _setup, _toUrl, _method } from "xisto:http";

declare function tlss_cred(cert: string, key: string): int;
declare function tlss_accept(cred: int, fd: int): int;
declare function tlss_read(h: int): Uint8Array;
declare function tlss_send(h: int, bytes: Uint8Array, off: int): int;
declare function tlss_write(h: int, bytes: Uint8Array, off: int): int;
declare function tlss_pending(h: int): boolean;
declare function tlss_end(h: int): boolean;
declare function tlss_close(h: int): void;
declare function tlsc_connect(host: string, port: int, servername: string): int;
declare function tlsc_last(): string;
declare function tlsc_read_buf(h: int): Uint8Array;
declare function tlsc_write(h: int, bytes: Uint8Array): int;
declare function tlsc_close(h: int): void;
declare function tls_add_ca(pem: string): void;
declare function net_shutdown(fd: int, how: int): void;
declare function net_code(code: int): string;

/** The options of createServer(): the certificate and its private key, PEM. */
export interface ServerOptions {
  key: string | Uint8Array;
  cert: string | Uint8Array;
}

function pem(v: string | Uint8Array): string {
  return typeof v === "string" ? (v as string) : new TextDecoder().decode(v as Uint8Array);
}

/** A server connection over TLS (the handshake happens within the first reads). */
class TlsServerTransport extends HttpTransport {
  private h: int;
  constructor(fd: int, h: int) {
    super(fd);
    this.h = h;
  }
  override read(): Uint8Array {
    return tlss_read(this.h);
  }
  override send(b: Uint8Array, off: int): int {
    return tlss_send(this.h, b, off);
  }
  override writeAll(b: Uint8Array, off: int): int {
    return tlss_write(this.h, b, off);
  }
  override pending(): boolean {
    return tlss_pending(this.h);
  }
  override endWrite(): boolean {
    return tlss_end(this.h);
  }
  override shutdown(how: int): void {
    if (how === 1) tlss_end(this.h); // (close_notify first, as far as the socket takes it now)
    net_shutdown(this.fd, how);
  }
  override close(): void {
    tlss_close(this.h);
  }
  override encrypted(): boolean {
    return true;
  }
}

/** A client connection over TLS (connected and verified). */
class TlsClientTransport extends HttpTransport {
  private h: int;
  constructor(h: int) {
    super(-1);
    this.h = h;
  }
  override read(): Uint8Array {
    return tlsc_read_buf(this.h);
  }
  override send(b: Uint8Array, off: int): int {
    return tlsc_write(this.h, off === 0 ? b : b.subarray(off)) < 0 ? -7 : b.length - off;
  }
  override writeAll(b: Uint8Array, off: int): int {
    return tlsc_write(this.h, off === 0 ? b : b.subarray(off));
  }
  override shutdown(how: int): void {
    tlsc_close(this.h);
    this.h = -1;
  }
  override close(): void {
    tlsc_close(this.h);
  }
  override encrypted(): boolean {
    return true;
  }
}

/** Node's https.Server: an http server whose connections are TLS. */
export class Server extends HttpServer {
  private cred: int;
  constructor(options: ServerOptions, listener: RequestListener | null = null) {
    super(listener);
    this.cred = tlss_cred(pem(options.cert), pem(options.key));
  }
  protected override wrap(fd: int): HttpTransport {
    return new TlsServerTransport(fd, tlss_accept(this.cred, fd));
  }
}

/** The server class under the name xisto used before (https.Server). */
export type HttpsServer = Server;

/** A TLS server that calls the listener for each request. */
export function createServer(options: ServerOptions, listener: RequestListener | null = null): Server {
  return new Server(options, listener);
}

/** A request over TLS. */
export class HttpsClientRequest extends ClientRequest {
  protected override expected(): string {
    return "https:";
  }
  protected override defaultPort(): int {
    return 443;
  }
  protected override open(host: string, port: int): HttpTransport {
    const h = tlsc_connect(host, port, this.servername.length > 0 ? this.servername : host);
    if (h === -9) throw new HttpSystemError("ERR_TLS_CERT", "tls", ": " + tlsc_last());
    if (h < 0) {
      const code = net_code(h);
      throw new HttpSystemError(code, code === "ENOTFOUND" ? "getaddrinfo" : "connect", code === "ENOTFOUND" ? ` ${host}` : ` ${host === "localhost" ? "127.0.0.1" : host}:${port}`);
    }
    return new TlsClientTransport(h);
  }
}

/** Makes an https request: call end() (with a body) to send it. With options.ca, that CA is trusted too (for the
 * whole program, from now on). */
export function request(target: string | URL | RequestOptions, callback: ResponseListener | null = null): ClientRequest {
  if (typeof target !== "string" && !(target instanceof URL)) {
    const o = target as RequestOptions;
    if (o.ca !== null) tls_add_ca(pem(o.ca as string | Uint8Array));
  }
  return _setup(new HttpsClientRequest(_toUrl(target, "https:"), _method(target)), target, callback);
}

/** An https GET request, sent at once. */
export function get(target: string | URL | RequestOptions, callback: ResponseListener | null = null): ClientRequest {
  const req = request(target, callback);
  req.end();
  return req;
}
