// xisto:tls (node:tls): tls.connect, a client TLSSocket over the runtime's TLS (AWS-LC by default, or BearSSL).
// The certificate is always verified against the system's CAs, NODE_EXTRA_CA_CERTS and the ca option.
// Differences from Node (docs/docs/guides/node-compatibility.md): rejectUnauthorized: false is refused (an error,
// not a silent insecure connection); ca adds to the CAs the whole program trusts, for the connections made after;
// no tls.createServer (node:https serves TLS); the handshake error messages are the TLS library's; TLSSocket is an
// EventEmitter, not a stream.Duplex.
import { EventEmitter } from "xisto:events";

declare function tlsc_connect(host: string, port: int, servername: string): int;
declare function tlsc_last(): string;
declare function tlsc_read(h: int): Uint8Array;
declare function tlsc_write(h: int, bytes: Uint8Array): int;
declare function tlsc_shutdown(h: int): void;
declare function tlsc_close(h: int): void;
declare function tls_add_ca(pem: string): void;
declare function net_code(code: int): string;

/** The options of connect(). */
export interface ConnectionOptions {
  host?: string;
  port?: number;
  servername?: string;
  ca?: string | Uint8Array;
  rejectUnauthorized?: boolean;
}

/** A TLS error: Node's code (ECONNREFUSED...) or the handshake's failure. */
export class TLSError extends Error {
  code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}

function pem(v: string | Uint8Array): string {
  return typeof v === "string" ? (v as string) : new TextDecoder().decode(v as Uint8Array);
}

/** A verified TLS connection: 'secureConnect', 'data', 'end', 'close', 'error'. */
export class TLSSocket extends EventEmitter {
  authorized: boolean = false;
  encrypted: boolean = true;
  destroyed: boolean = false;
  bytesRead: int = 0;
  bytesWritten: int = 0;
  private h: int = -1;
  private encoding: string = "";
  private pending: Uint8Array[] = [];
  private ending: boolean = false;
  private hadError: boolean = false;
  private task: Promise<void> | null = null;

  /** (internal) starts connecting. */
  _start(host: string, port: int, servername: string): void {
    this.task = this.run(host, port, servername);
  }
  private async run(host: string, port: int, servername: string): Promise<void> {
    const h = tlsc_connect(host, port, servername);
    if (h < 0) {
      this.hadError = true;
      if (h === -9) this.emit("error", new TLSError("ERR_TLS_HANDSHAKE", tlsc_last()));
      else this.emit("error", new TLSError(net_code(h), `connect ${net_code(h)} ${host}:${port}`));
      this.destroyed = true;
      this.emit("close", true);
      return;
    }
    this.h = h;
    this.authorized = true;
    this.emit("secureConnect");
    for (const b of this.pending) this.send(b);
    this.pending = [];
    if (this.ending) tlsc_shutdown(this.h);
    for (;;) {
      const chunk = tlsc_read(this.h);
      if (chunk.length === 0) break;
      this.bytesRead += chunk.length;
      if (this.encoding.length > 0) this.emit("data", new TextDecoder().decode(chunk));
      else this.emit("data", Buffer.from(chunk));
    }
    if (!this.destroyed) this.emit("end");
    tlsc_close(this.h);
    this.h = -1;
    this.destroyed = true;
    this.emit("close", this.hadError);
  }
  private send(bytes: Uint8Array): void {
    if (tlsc_write(this.h, bytes) < 0) {
      this.hadError = true;
      this.emit("error", new TLSError("EPIPE", "write EPIPE"));
      return;
    }
    this.bytesWritten += bytes.length;
  }
  setEncoding(encoding: string = "utf8"): TLSSocket {
    this.encoding = encoding;
    return this;
  }
  write(data: string | Uint8Array, callback: (() => void) | null = null): boolean {
    const bytes: Uint8Array = typeof data === "string" ? new TextEncoder().encode(data as string) : (data as Uint8Array);
    if (this.h < 0) this.pending.push(bytes);
    else this.send(bytes);
    if (callback !== null) setImmediate(callback);
    return true;
  }
  /** Writes the last data and closes the connection (TLS has no half-close here: the peer sees the end). */
  end(data: string | Uint8Array | null = null): TLSSocket {
    if (data !== null) this.write(data);
    this.ending = true;
    if (this.h >= 0) tlsc_shutdown(this.h);
    return this;
  }
  destroy(): TLSSocket {
    if (this.destroyed) return this;
    this.destroyed = true;
    if (this.h >= 0) tlsc_shutdown(this.h);
    return this;
  }
}

/** Connects to port on host and verifies its certificate; the listener runs on 'secureConnect'. */
export function connect(options: ConnectionOptions, listener: (() => void) | null = null): TLSSocket {
  if (options.rejectUnauthorized === false)
    throw new TypeError("tls.connect: rejectUnauthorized: false is not supported (xisto verifies every certificate; pass the server's CA as ca)");
  if (options.ca !== null) tls_add_ca(pem(options.ca as string | Uint8Array));
  const host = options.host ?? "localhost";
  const s = new TLSSocket();
  if (listener !== null) s.once("secureConnect", listener);
  s._start(host, Math.trunc(options.port ?? 443), options.servername ?? host);
  return s;
}
