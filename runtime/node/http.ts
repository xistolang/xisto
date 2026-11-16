// xisto:http (node:http): createServer(listener) with IncomingMessage and ServerResponse, server.listen/close/address,
// and the client request()/get(), written over node:net's sockets (each connection a task of the event loop, as the
// sockets of node:net are). Bodies are bytes: a request's 'data' gives Buffers (strings after setEncoding), and
// write()/end() take strings, Buffers and Uint8Arrays. A response is streamed: its head goes out with the first write()
// (or flushHeaders()), each write is sent at once (chunked when no Content-Length was set, as Node does), write()
// returns false above the high-water mark and 'drain' follows; end() alone sends a Content-Length. Any number of
// servers, each on its own port. IncomingMessage is a stream.Readable, ServerResponse and ClientRequest are
// stream.Writables (pipe() into them). Differences from Node: docs/docs/guides/node-compatibility.md.
import { EventEmitter } from "xisto:events";
import { Readable, Writable, ErrorCallback } from "xisto:stream";
import { channel } from "xisto:diagnostics_channel";
import { Socket } from "xisto:net";

declare function net_listen(host: string, port: int): int;
declare function net_accept(lfd: int): int;
declare function net_connect(host: string, port: int): int;
declare function net_read_buf(fd: int, drained: boolean): Uint8Array;
declare function net_read_error(): int;
declare function net_write(fd: int, bytes: Uint8Array): int;
declare function net_send(fd: int, bytes: Uint8Array, off: int): int;
declare function net_shutdown(fd: int, how: int): void;
declare function net_close(fd: int): void;
declare function net_code(code: int): string;
declare function net_port(fd: int, peer: boolean): int;
declare function net_address(fd: int, peer: boolean): string;
declare function net_wake(port: int): void;
declare function nhttp_parse(bytes: Uint8Array, start: int, end: int): string[];
declare function nhttp_end(): int;
declare function net_idle_alive(fd: int): boolean;

/** The reason phrase of each status code. */
export const STATUS_CODES: Record<string, string> = {
  "100": "Continue", "101": "Switching Protocols", "102": "Processing", "103": "Early Hints",
  "200": "OK", "201": "Created", "202": "Accepted", "203": "Non-Authoritative Information", "204": "No Content",
  "205": "Reset Content", "206": "Partial Content", "207": "Multi-Status", "208": "Already Reported", "226": "IM Used",
  "300": "Multiple Choices", "301": "Moved Permanently", "302": "Found", "303": "See Other", "304": "Not Modified",
  "305": "Use Proxy", "307": "Temporary Redirect", "308": "Permanent Redirect",
  "400": "Bad Request", "401": "Unauthorized", "402": "Payment Required", "403": "Forbidden", "404": "Not Found",
  "405": "Method Not Allowed", "406": "Not Acceptable", "407": "Proxy Authentication Required", "408": "Request Timeout",
  "409": "Conflict", "410": "Gone", "411": "Length Required", "412": "Precondition Failed", "413": "Payload Too Large",
  "414": "URI Too Long", "415": "Unsupported Media Type", "416": "Range Not Satisfiable", "417": "Expectation Failed",
  "418": "I'm a Teapot", "421": "Misdirected Request", "422": "Unprocessable Entity", "423": "Locked",
  "424": "Failed Dependency", "425": "Too Early", "426": "Upgrade Required", "428": "Precondition Required",
  "429": "Too Many Requests", "431": "Request Header Fields Too Large", "451": "Unavailable For Legal Reasons",
  "500": "Internal Server Error", "501": "Not Implemented", "502": "Bad Gateway", "503": "Service Unavailable",
  "504": "Gateway Timeout", "505": "HTTP Version Not Supported", "506": "Variant Also Negotiates",
  "507": "Insufficient Storage", "508": "Loop Detected", "509": "Bandwidth Limit Exceeded", "510": "Not Extended",
  "511": "Network Authentication Required",
};

/** The methods Node's parser knows. */
export const METHODS: string[] = [
  "ACL", "BIND", "CHECKOUT", "CONNECT", "COPY", "DELETE", "GET", "HEAD", "LINK", "LOCK", "M-SEARCH", "MERGE",
  "MKACTIVITY", "MKCALENDAR", "MKCOL", "MOVE", "NOTIFY", "OPTIONS", "PATCH", "POST", "PROPFIND", "PROPPATCH",
  "PURGE", "PUT", "QUERY", "REBIND", "REPORT", "SEARCH", "SOURCE", "SUBSCRIBE", "TRACE", "UNBIND", "UNLINK",
  "UNLOCK", "UNSUBSCRIBE",
];

/** The largest head a message may have (Node's --max-http-header-size default is 16 KB; the parser takes 64 KB). */
export const maxHeaderSize: int = 65536;

const EMPTY: Uint8Array = Buffer.alloc(0);
/** Bytes a connection may hold unsent before write() says to wait for 'drain' (a Writable's default). */
const HIGH_WATER: int = 16384;

function reason(code: int): string {
  const k = String(code);
  return STATUS_CODES[k] ?? "unknown";
}

let dateSec: number = -1;
let dateText: string = "";
/** The Date header's value, made once a second. */
function httpDate(): string {
  const sec = Math.floor(Date.now() / 1000);
  if (sec !== dateSec) {
    dateSec = sec;
    dateText = new Date(sec * 1000).toUTCString();
  }
  return dateText;
}

/** A chunk given to write()/end() as bytes (a string in its encoding, UTF-8 by default). */
function toBytes(chunk: any, encoding: string | null): Uint8Array {
  if (typeof chunk === "string") {
    if (encoding === null || encoding === "utf8" || encoding === "utf-8") return Buffer.from(chunk as string);
    return Buffer.from(chunk as string, encoding);
  }
  return chunk as Uint8Array;
}

/** The bytes of several arrays in one (a Buffer). */
function concat(parts: Uint8Array[]): Uint8Array {
  let n: int = 0;
  for (const p of parts) n += p.length;
  const out = Buffer.alloc(n);
  let at: int = 0;
  for (const p of parts) {
    out.set(p, at);
    at += p.length;
  }
  return out;
}

/** A socket error, with Node's code and message. */
export class HttpSystemError extends Error {
  code: string;
  syscall: string;
  constructor(code: string, syscall: string, detail: string) {
    super(`${syscall} ${code}${detail}`);
    this.code = code;
    this.syscall = syscall;
  }
}

/** (internal) the socket under one connection: plain TCP here, TLS in node:https (which overrides each method).
 * read() and writeAll() park the task that calls them; send() never does. */
export class HttpTransport {
  fd: int;
  private drained: boolean = false;
  constructor(fd: int) {
    this.fd = fd;
  }
  /** The next bytes (a Buffer); empty at the end or on an error. */
  read(): Uint8Array {
    const b = net_read_buf(this.fd, this.drained);
    this.drained = b.length < 65536;
    return b;
  }
  /** Writes what the socket takes now: the bytes written (0: it is full), or a negative error code. */
  send(b: Uint8Array, off: int): int {
    return net_send(this.fd, b, off);
  }
  /** Writes all of it (parks while the socket is full): 0, or a negative error code. */
  writeAll(b: Uint8Array, off: int): int {
    return net_write(this.fd, off === 0 ? b : b.subarray(off));
  }
  /** true while bytes send() took are still held (TLS records the socket did not take yet): writeAll(EMPTY, 0)
   * writes them out. */
  pending(): boolean {
    return false;
  }
  /** Starts the end of the writing side (TLS: close_notify); true when nothing is left to write before shutdown(1). */
  endWrite(): boolean {
    return true;
  }
  /** 0: no more reads, 1: no more writes (the peer reads the end), 2: both. */
  shutdown(how: int): void {
    net_shutdown(this.fd, how);
  }
  close(): void {
    net_close(this.fd);
  }
  encrypted(): boolean {
    return false;
  }
  /** (http.Agent) an idle connection that can carry another request: still open, nothing unread on it. */
  alive(): boolean {
    return net_idle_alive(this.fd);
  }
}

/** The connection under a request or response (req.socket, res.socket): its addresses, destroy(). */
export class HttpSocket extends EventEmitter {
  encrypted: boolean;
  destroyed: boolean = false;
  private t: HttpTransport;
  private raddr: string = "";
  private laddr: string = "";
  private rport: int = -1;
  private lport: int = -1;
  constructor(t: HttpTransport) {
    super();
    this.t = t;
    this.encrypted = t.encrypted();
  }
  get remoteAddress(): string {
    if (this.raddr.length === 0 && !this.destroyed) this.raddr = net_address(this.t.fd, true);
    return this.raddr;
  }
  get remotePort(): int {
    if (this.rport < 0 && !this.destroyed) this.rport = net_port(this.t.fd, true);
    return this.rport;
  }
  get localAddress(): string {
    if (this.laddr.length === 0 && !this.destroyed) this.laddr = net_address(this.t.fd, false);
    return this.laddr;
  }
  get localPort(): int {
    if (this.lport < 0 && !this.destroyed) this.lport = net_port(this.t.fd, false);
    return this.lport;
  }
  get remoteFamily(): string {
    return this.remoteAddress.includes(":") ? "IPv6" : "IPv4";
  }
  /** Closes the connection at once (a response in progress is cut). */
  destroy(err: Error | null = null): HttpSocket {
    if (!this.destroyed) this.t.shutdown(2);
    return this;
  }
  /** Ends the writing side (the peer reads the end). */
  end(): HttpSocket {
    if (!this.destroyed) this.t.shutdown(1);
    return this;
  }
  setTimeout(ms: int, callback: (() => void) | null = null): HttpSocket {
    return this;
  }
  setNoDelay(noDelay: boolean = true): HttpSocket {
    return this; // (always on)
  }
  setKeepAlive(enable: boolean = false, initialDelay: int = 0): HttpSocket {
    return this;
  }
  ref(): HttpSocket {
    return this;
  }
  unref(): HttpSocket {
    return this;
  }
  /** (internal) the connection is closed. */
  _closed(): void {
    if (this.destroyed) return;
    this.destroyed = true;
    this.emit("close", false);
  }
}

/** (the socket of a message not on a connection yet) */
const NO_SOCKET: HttpSocket = new HttpSocket(new HttpTransport(-1));

/** A request the server received, or a response the client got: status/method, headers and the body as a Readable. */
export class IncomingMessage extends Readable {
  method: string = "";
  url: string = "";
  statusCode: int = 0;
  statusMessage: string = "";
  httpVersion: string = "1.1";
  httpVersionMajor: int = 1;
  httpVersionMinor: int = 1;
  /** The headers by lower-case name (several of one name joined with ", ", cookies with "; "). */
  headers: Record<string, string> = {};
  /** The headers as they came: name, value, name, value... */
  rawHeaders: string[] = [];
  trailers: Record<string, string> = {};
  rawTrailers: string[] = [];
  /** true once the whole body was received */
  complete: boolean = false;
  /** true when the connection closed before the body (server) or the response (client) was complete */
  aborted: boolean = false;
  socket: HttpSocket = NO_SOCKET;
  /** for node:diagnostics_channel's subscribers (an APM agent's span for this request) */
  _diag: any = null;
  private closeSent: boolean = false;

  constructor() {
    super({ autoDestroy: false, emitClose: false });
  }
  get connection(): HttpSocket {
    return this.socket;
  }
  /** (the body is pushed as it arrives: nothing to read on demand) */
  _read(size: number): void {}
  /** (internal) the head's fields: the headers' name, value pairs from index 3 of parts. */
  _setHeaders(parts: string[]): void {
    for (let i = 3; i + 1 < parts.length; i += 2) {
      const name = parts[i];
      const v = parts[i + 1];
      this.rawHeaders.push(name);
      this.rawHeaders.push(v);
      const k = name.toLowerCase();
      const cur = this.headers[k];
      if (cur === null) this.headers[k] = v;
      else this.headers[k] = cur + (k === "cookie" ? "; " : ", ") + v;
    }
  }
  /** (internal) the HTTP version of the head ("HTTP/1.1"). */
  _setVersion(v: string): void {
    if (v.length === 8 && v.startsWith("HTTP/")) {
      this.httpVersion = v.slice(5);
      this.httpVersionMajor = Math.trunc(v.charCodeAt(5)) - 48;
      this.httpVersionMinor = Math.trunc(v.charCodeAt(7)) - 48;
    }
  }
  /** (internal) more of the body */
  _data(chunk: Uint8Array): void {
    this.push(chunk);
  }
  /** (internal) the body is complete */
  _end(): void {
    this.complete = true;
    this.push(null);
  }
  /** (internal) 'close', once */
  _close(): void {
    if (this.closeSent) return;
    this.closeSent = true;
    this.emit("close");
  }
  setTimeout(ms: int, callback: (() => void) | null = null): IncomingMessage {
    return this;
  }
}

/** The response a request listener writes: status, headers, then the body with write() and end(). */
export class ServerResponse extends Writable {
  statusCode: int = 200;
  statusMessage: string = "";
  /** true once the head is fixed (writeHead(), or the first write) */
  headersSent: boolean = false;
  sendDate: boolean = true;
  /** the body goes out in chunks (no Content-Length was known when the head was sent) */
  chunkedEncoding: boolean = false;
  /** the request this response answers */
  req: IncomingMessage;
  socket: HttpSocket = NO_SOCKET;
  private names: Map<string, string> = new Map<string, string>(); // lower-case name -> name as set
  private values: Map<string, string[]> = new Map<string, string[]>();
  private conn: HttpConnection | null = null;
  private keepAlive: boolean = true;
  private noBody: boolean = false; // a HEAD request, or a status without a body
  private head: boolean = false; // a HEAD request
  private sent: boolean = false; // the head is on its way
  private headStr: string = ""; // the head writeHead() fixed
  private ended: boolean = false;
  private finishSent: boolean = false;
  private closeSent: boolean = false;
  private cbs: ErrorCallback[] = [];

  constructor(req: IncomingMessage) {
    super(null);
    this.req = req;
  }
  /** (internal) the connection the response goes to. */
  _attach(conn: HttpConnection, keepAlive: boolean, head: boolean): void {
    this.conn = conn;
    this.socket = conn.sock;
    this.keepAlive = keepAlive;
    this.head = head;
  }
  get connection(): HttpSocket {
    return this.socket;
  }
  /** true once end() was called */
  get finished(): boolean {
    return this.ended;
  }

  /** Sets a header (replacing one of the same name, in any case). */
  setHeader(name: string, value: string | number | string[]): ServerResponse {
    if (this.headersSent) throw new Error("Cannot set headers after they are sent to the client");
    const k = name.toLowerCase();
    this.names.set(k, name);
    if (Array.isArray(value)) this.values.set(k, (value as string[]).slice());
    else this.values.set(k, [String(value)]);
    return this;
  }
  /** Adds a value to a header (set-cookie: one line each). */
  appendHeader(name: string, value: string | string[]): ServerResponse {
    if (this.headersSent) throw new Error("Cannot append headers after they are sent to the client");
    const k = name.toLowerCase();
    const cur = this.values.get(k);
    const add: string[] = Array.isArray(value) ? (value as string[]) : [value as string];
    if (cur === null) {
      this.names.set(k, name);
      this.values.set(k, add.slice());
    } else {
      for (const v of add) cur.push(v);
    }
    return this;
  }
  /** The value of a header (several values joined with ", "), or null. */
  getHeader(name: string): string | null {
    const v = this.values.get(name.toLowerCase());
    return v === null ? null : v.join(", ");
  }
  getHeaders(): Record<string, string> {
    const out: Record<string, string> = {};
    for (const k of this.values.keys()) out[k] = (this.values.get(k) ?? []).join(", ");
    return out;
  }
  getHeaderNames(): string[] {
    const out: string[] = [];
    for (const k of this.values.keys()) out.push(k);
    return out;
  }
  hasHeader(name: string): boolean {
    return this.values.has(name.toLowerCase());
  }
  removeHeader(name: string): void {
    if (this.headersSent) throw new Error("Cannot remove headers after they are sent to the client");
    const k = name.toLowerCase();
    this.values.delete(k);
    this.names.delete(k);
  }
  /** Sets the status (and its reason phrase) and headers at once: writeHead(code, headers) or
   * writeHead(code, reason, headers). The head goes out with the first write() or end(). */
  writeHead(statusCode: int, reasonOrHeaders: string | Record<string, string | number | string[]> | null = null, headers: Record<string, string | number | string[]> | null = null): ServerResponse {
    if (this.headersSent) throw new Error("Cannot write headers after they are sent to the client");
    this.statusCode = statusCode;
    let hs: Record<string, string | number | string[]> | null = headers;
    if (typeof reasonOrHeaders === "string") this.statusMessage = reasonOrHeaders as string;
    else if (reasonOrHeaders !== null) hs = reasonOrHeaders as Record<string, string | number | string[]>;
    if (hs !== null) {
      for (const k of Object.keys(hs)) {
        const v = hs[k];
        if (v !== null) this.setHeader(k, v);
      }
    }
    this.headersSent = true;
    const conn = this.conn;
    if (conn !== null) this.headStr = this.headText(null, false, conn); // (fixed now, as Node's: the body's length is not known yet)
    return this;
  }
  /** Sends the head now (a server-sent events stream, a long poll). */
  flushHeaders(): void {
    if (this.sent || this.conn === null) return;
    this.headersSent = true;
    this.out(null, false);
  }
  /** Sends 100 Continue (to a request with Expect: 100-continue, from a 'checkContinue' listener). */
  writeContinue(): void {
    if (this.conn !== null) this.conn.send(Buffer.from("HTTP/1.1 100 Continue\r\n\r\n"));
  }
  setTimeout(ms: int, callback: (() => void) | null = null): ServerResponse {
    return this;
  }

  /** Sends a chunk of the body (the head first, if it has not gone yet). false: the connection holds more than the
   * high-water mark, wait for 'drain'. write(chunk, cb) and write(chunk, encoding, cb) as in Node. */
  override write(chunk: any, encoding: string | ErrorCallback | null = null, cb: ErrorCallback | null = null): boolean {
    let enc: string | null = null;
    let f: ErrorCallback | null = cb;
    if (typeof encoding === "string") enc = encoding as string;
    else if (encoding !== null) f = encoding as ErrorCallback;
    if (this.ended) {
      const err = new Error("write after end");
      process.nextTick(() => {
        this.emit("error", err);
      });
      if (f !== null) {
        const g = f as ErrorCallback;
        process.nextTick(() => g(err));
      }
      return false;
    }
    if (chunk === null) throw new TypeError('The "chunk" argument must be of type string or an instance of Buffer or Uint8Array. Received null');
    this.headersSent = true;
    const ok = this.out(toBytes(chunk, enc), false);
    if (f !== null) this.later(f as ErrorCallback);
    return ok;
  }
  /** Ends the response (with a last chunk): what was not sent goes now; 'finish' once it is all written.
   * end(cb), end(chunk, cb), end(chunk, encoding, cb) as in Node. */
  override end(chunk: any = null, encoding: string | ErrorCallback | null = null, cb: ErrorCallback | null = null): ServerResponse {
    let c: any = chunk;
    let enc: string | null = null;
    let f: ErrorCallback | null = cb;
    if (typeof encoding === "string") enc = encoding as string;
    else if (encoding !== null) f = encoding as ErrorCallback;
    if (c !== null && typeof c === "function") { // (end(cb))
      f = c as ErrorCallback;
      c = null;
    }
    if (this.ended) return this;
    this.ended = true;
    this.headersSent = true;
    const ws = this._writableState;
    if (ws !== null) {
      ws.ending = true;
      ws.ended = true;
    }
    if (f !== null) {
      const g = f as ErrorCallback;
      this.once("finish", () => g(null));
    }
    if (this.conn === null) return this;
    this.out(c === null ? null : toBytes(c, enc), true);
    const conn = this.conn as HttpConnection;
    conn._responseEnded(this, this.keepAlive);
    return this;
  }
  /** (internal) a write's callback: once the connection has written what it holds. */
  private later(f: ErrorCallback): void {
    const conn = this.conn;
    if (conn === null || conn.queued === 0) process.nextTick(() => f(null));
    else this.cbs.push(f);
  }
  /** (internal) the head (once) and a chunk of the body, framed, to the connection. */
  private out(data: Uint8Array | null, last: boolean): boolean {
    const conn = this.conn;
    if (conn === null || conn.gone) return false;
    let head = "";
    if (!this.sent) {
      this.sent = true;
      this.headersSent = true;
      head = this.headStr.length > 0 ? this.headStr : this.headText(data, last, conn);
      this.headStr = "";
    }
    let body: Uint8Array | null = this.noBody ? null : data;
    if (body !== null && body.length === 0) body = null;
    let pre = head;
    let post = "";
    if (this.chunkedEncoding) {
      if (body !== null) {
        pre += body.length.toString(16) + "\r\n";
        post = "\r\n";
      }
      if (last) post += "0\r\n\r\n";
    }
    let ok = true;
    if (body === null) {
      const all = pre + post;
      if (all.length > 0) ok = conn.send(Buffer.from(all));
    } else if (pre.length === 0 && post.length === 0) {
      ok = conn.send(body);
    } else {
      const parts: Uint8Array[] = [];
      if (pre.length > 0) parts.push(Buffer.from(pre));
      parts.push(body);
      if (post.length > 0) parts.push(Buffer.from(post));
      ok = conn.send(concat(parts));
    }
    if (!ok) {
      const ws = this._writableState;
      if (ws !== null) ws.needDrain = true;
    }
    return ok;
  }
  /** (internal) the status line and headers, the framing decided: Content-Length when the whole body is known
   * (end() before any write), chunked otherwise (HTTP/1.0: the end of the connection ends the body). */
  private headText(data: Uint8Array | null, last: boolean, conn: HttpConnection): string {
    const code = this.statusCode;
    if (code === 204 || code === 304 || (code >= 100 && code < 200)) this.noBody = true;
    if (this.head) this.noBody = true;
    const msg = this.statusMessage.length > 0 ? this.statusMessage : reason(code);
    if (this.statusMessage.length === 0) this.statusMessage = msg;
    let h = `HTTP/1.1 ${code} ${msg}\r\n`;
    for (const k of this.values.keys()) {
      const name = this.names.get(k) ?? k;
      for (const v of this.values.get(k) ?? []) h += name + ": " + v + "\r\n";
    }
    if (this.sendDate && !this.values.has("date")) h += "Date: " + httpDate() + "\r\n";
    const hasLen = this.values.has("content-length");
    const te = this.values.get("transfer-encoding");
    const userChunked = te !== null && te.join(",").toLowerCase().includes("chunked");
    if (conn.srv.closing() || this.req.httpVersionMinor === 0 && this.req.httpVersionMajor === 1 && !conn.http10KeepAlive) this.keepAlive = false;
    const connHdr = this.values.get("connection");
    if (connHdr !== null && connHdr.join(",").toLowerCase().includes("close")) this.keepAlive = false;
    let framing = "";
    if (userChunked) {
      this.chunkedEncoding = !this.noBody;
    } else if (hasLen || this.noBody) {
      // (the length is the user's, or there is no body)
    } else if (last) {
      framing = "Content-Length: " + String(data === null ? 0 : data.length) + "\r\n";
    } else if (this.req.httpVersionMinor >= 1) {
      this.chunkedEncoding = true;
      framing = "Transfer-Encoding: chunked\r\n";
    } else {
      this.keepAlive = false; // (HTTP/1.0 without a length: the body ends with the connection)
    }
    if (connHdr === null) {
      if (this.keepAlive) h += "Connection: keep-alive\r\nKeep-Alive: timeout=" + String(Math.floor(conn.srv.keepAliveTimeout / 1000)) + "\r\n";
      else h += "Connection: close\r\n";
    }
    return h + framing + "\r\n";
  }
  /** (internal) the connection wrote all it held: the callbacks of write(), 'drain' */
  _drained(): void {
    if (this.cbs.length > 0) {
      const cbs = this.cbs;
      this.cbs = [];
      for (const f of cbs) f(null);
    }
    const ws = this._writableState;
    if (ws !== null && ws.needDrain) {
      ws.needDrain = false;
      if (!this.ended) this.emit("drain");
    }
  }
  /** (internal) everything was written: 'finish', then 'close' (the request's 'close' after). */
  _finish(srv: Server | null): void {
    if (this.finishSent) return;
    this.finishSent = true;
    const ws = this._writableState;
    if (ws !== null) ws.finished = true;
    const finCh = channel("http.server.response.finish");
    if (finCh.hasSubscribers) {
      const m = new HttpChannelMessage(this.req);
      m.response = this;
      m.server = srv;
      finCh.publish(m);
    }
    this.emit("finish");
    setImmediate(() => {
      this._close();
    });
  }
  /** (internal) 'close' once: after 'finish', or when the connection went away first. */
  _close(): void {
    if (this.closeSent) return;
    this.closeSent = true;
    const ws = this._writableState;
    if (ws !== null) ws.closed = true;
    this.emit("close");
    this.req._close();
  }
}

/** What server.address() gives. */
export class AddressInfo {
  address: string;
  family: string;
  port: int;
  constructor(address: string, family: string, port: int) {
    this.address = address;
    this.family = family;
    this.port = port;
  }
}

export type RequestListener = (req: IncomingMessage, res: ServerResponse) => void;

/** The message of node:diagnostics_channel's http.server.* and http.client.* channels (as Node's). */
export class HttpChannelMessage {
  request: any;
  response: any = null;
  server: Server | null = null;
  error: Error | null = null;
  constructor(request: any) {
    this.request = request;
  }
}

/** Runs the request listeners on a task of their own: one that awaits does not hold the connection's reading. */
async function dispatch(srv: Server, msg: IncomingMessage, res: ServerResponse): Promise<void> {
  const startCh = channel("http.server.request.start");
  if (startCh.hasSubscribers) {
    const m = new HttpChannelMessage(msg);
    m.response = res;
    m.server = srv;
    startCh.publish(m);
  }
  srv.emit("request", msg, res);
}

/** (internal) one connection of a server: reads requests (a task parked on the socket), writes responses in order. */
export class HttpConnection {
  id: int;
  srv: Server;
  t: HttpTransport;
  sock: HttpSocket;
  /** bytes waiting to be written */
  queued: int = 0;
  /** the peer left (or the socket failed): nothing more is written */
  gone: boolean = false;
  /** HTTP/1.0 request that asked for keep-alive */
  http10KeepAlive: boolean = false;
  /** Date.now() when the connection last became idle */
  idleSince: number = 0;
  private buf: Uint8Array = EMPTY;
  private pos: int = 0;
  private state: int = 0; // 0: a head; 1: a body; 2: the response of the last request is not ended; 3: closing
  private left: int = 0;
  private chunked: boolean = false;
  private chunkLeft: int = -1; // -1: a size line; > 0: bytes of data; -3: the CRLF after the data; -2: trailers
  private req: IncomingMessage | null = null;
  private res: ServerResponse | null = null; // the response being written (the last request's)
  private finishing: ServerResponse[] = []; // ended, waiting for their bytes to be written
  private out: Uint8Array[] = [];
  private flushing: boolean = false;
  private flushTask: Promise<void> | null = null;
  private closeAfter: boolean = false;
  private reading: boolean = true;
  private closed: boolean = false;
  task: Promise<void> | null = null;
  private handler: Promise<void> | null = null;

  constructor(srv: Server, t: HttpTransport, id: int) {
    this.srv = srv;
    this.t = t;
    this.id = id;
    this.sock = new HttpSocket(t);
    this.idleSince = Date.now();
  }
  /** true when no request is in progress (server.close() ends such connections at once) */
  idle(): boolean {
    return this.state === 0 && this.pos >= this.buf.length && this.res === null && this.finishing.length === 0;
  }
  /** Reads until the end of the connection, handing each request to the server as its head arrives. */
  async run(): Promise<void> {
    for (;;) {
      const chunk = this.t.read();
      if (chunk.length === 0) break;
      if (this.state === 3) continue; // (closing: what comes is ignored)
      if (this.pos >= this.buf.length) {
        this.buf = chunk;
        this.pos = 0;
      } else {
        this.buf = concat([this.buf.subarray(this.pos), chunk]);
        this.pos = 0;
      }
      this.process();
    }
    this.reading = false;
    this.lost();
  }
  /** Handles what the buffer holds: heads, bodies; stops while a response is not ended (requests go in order). */
  private process(): void {
    for (;;) {
      if (this.state === 0) {
        if (this.pos >= this.buf.length) {
          this.buf = EMPTY;
          this.pos = 0;
          return;
        }
        const parts = nhttp_parse(this.buf, this.pos, this.buf.length);
        const end = nhttp_end();
        if (end === -1) return;
        if (end === -2) {
          this.reject(400, "Bad Request");
          return;
        }
        this.pos = end;
        this.start(parts);
      } else if (this.state === 1) {
        if (!this.feed()) return;
      } else {
        return;
      }
    }
  }
  /** A malformed request: an answer, then the end of the connection (as Node's parser errors). */
  private reject(code: int, text: string): void {
    this.state = 3;
    this.send(Buffer.from(`HTTP/1.1 ${code} ${text}\r\nConnection: close\r\n\r\n`));
    this.endAfterWrites();
  }
  /** A request's head arrived: its IncomingMessage and ServerResponse, then the listeners. */
  private start(parts: string[]): void {
    const msg = new IncomingMessage();
    msg.socket = this.sock;
    msg.method = parts[0];
    msg.url = parts[1];
    const version = parts[2];
    if (!version.startsWith("HTTP/1.")) {
      this.reject(version.startsWith("HTTP/") ? 505 : 400, version.startsWith("HTTP/") ? "HTTP Version Not Supported" : "Bad Request");
      return;
    }
    msg._setVersion(version);
    msg._setHeaders(parts);
    const te = msg.headers["transfer-encoding"];
    const cl = msg.headers["content-length"];
    this.chunked = false;
    this.left = 0;
    if (te !== null && cl !== null) { // (both: a request smuggling vector, which Node's parser answers with 400)
      this.reject(400, "Bad Request");
      return;
    }
    if (te !== null && te.toLowerCase().includes("chunked")) {
      this.chunked = true;
      this.chunkLeft = -1;
    } else if (cl !== null) {
      const n = Number(cl);
      if (!(n >= 0) || Math.floor(n) !== n) {
        this.reject(400, "Bad Request");
        return;
      }
      this.left = Math.floor(n);
    }
    const conn = msg.headers["connection"];
    const connLc = conn === null ? "" : conn.toLowerCase();
    let keepAlive = msg.httpVersionMinor >= 1 ? !connLc.includes("close") : connLc.includes("keep-alive");
    this.http10KeepAlive = msg.httpVersionMinor === 0 && keepAlive;
    if (this.srv.closing()) keepAlive = false;
    const res = new ServerResponse(msg);
    res._attach(this, keepAlive, msg.method === "HEAD");
    this.req = msg;
    this.res = res;
    this.state = 1;
    const expect = msg.headers["expect"];
    const srv = this.srv;
    if (expect !== null && expect.toLowerCase() === "100-continue") {
      if (srv.listenerCount("checkContinue") > 0) {
        srv.emit("checkContinue", msg, res);
        if (!this.chunked && this.left === 0) this.bodyDone();
        return;
      }
      res.writeContinue();
    }
    this.handler = dispatch(srv, msg, res); // (a task of its own; it runs now, until its first wait)
    if (!this.chunked && this.left === 0) this.bodyDone();
  }
  /** The body bytes in the buffer to the request; true once the body is complete. */
  private feed(): boolean {
    const msg = this.req as IncomingMessage;
    if (!this.chunked) {
      const avail = this.buf.length - this.pos;
      const n = avail < this.left ? avail : this.left;
      if (n > 0) {
        msg._data(this.buf.subarray(this.pos, this.pos + n));
        this.pos += n;
        this.left -= n;
      }
      if (this.left > 0) return false;
      this.bodyDone();
      return true;
    }
    for (;;) {
      const avail = this.buf.length - this.pos;
      if (this.chunkLeft > 0) {
        if (avail === 0) return false;
        const n = avail < this.chunkLeft ? avail : this.chunkLeft;
        msg._data(this.buf.subarray(this.pos, this.pos + n));
        this.pos += n;
        this.chunkLeft -= n;
        if (this.chunkLeft === 0) this.chunkLeft = -3;
      } else if (this.chunkLeft === -3) {
        if (avail < 2) return false;
        this.pos += 2;
        this.chunkLeft = -1;
      } else {
        const nl = this.buf.indexOf(10, this.pos);
        if (nl < 0) {
          if (avail > 8192) this.reject(400, "Bad Request");
          return false;
        }
        const line = Buffer.from(this.buf.subarray(this.pos, nl)).toString("latin1").trim();
        this.pos = nl + 1;
        if (this.chunkLeft === -2) {
          if (line.length === 0) {
            this.bodyDone();
            return true;
          }
          const colon = line.indexOf(":");
          if (colon > 0) {
            const k = line.slice(0, colon).trim();
            const v = line.slice(colon + 1).trim();
            msg.rawTrailers.push(k);
            msg.rawTrailers.push(v);
            msg.trailers[k.toLowerCase()] = v;
          }
          continue;
        }
        const semi = line.indexOf(";");
        const hex = semi >= 0 ? line.slice(0, semi).trim() : line;
        const size = hexSize(hex);
        if (size < 0) {
          this.reject(400, "Bad Request");
          return false;
        }
        this.chunkLeft = size === 0 ? -2 : Math.trunc(size);
      }
    }
  }
  /** The request's body is complete: its 'end'; the next request waits for this one's response. */
  private bodyDone(): void {
    const msg = this.req as IncomingMessage;
    this.req = null;
    if (this.state === 3) return;
    this.state = this.res === null ? 0 : 2;
    msg._end();
  }
  /** (internal) a response was ended: the next request (if any) can be answered; the response finishes once its bytes
   * are written. */
  _responseEnded(res: ServerResponse, keepAlive: boolean): void {
    if (this.res === res) this.res = null;
    if (this.flushing) this.finishing.push(res);
    else {
      const srv = this.srv;
      process.nextTick(() => res._finish(srv));
    }
    if (!keepAlive || this.state === 3) {
      if (this.state !== 3) this.state = 3;
      this.endAfterWrites();
      return;
    }
    this.idleSince = Date.now();
    if (this.state === 2) {
      this.state = 0;
      this.process();
    }
    if (this.srv.closing() && this.idle()) this.endAfterWrites();
  }
  /** Ends the writing side once all is written (the peer then closes, and so does the reading). */
  private endAfterWrites(): void {
    this.state = 3;
    if (this.flushing) this.closeAfter = true;
    else if (!this.closed) this.endWriting();
  }
  /** The end of the writing side once the transport has written all it holds (TLS: its records, then close_notify). */
  private endWriting(): void {
    if (this.t.endWrite()) {
      this.t.shutdown(1);
      return;
    }
    this.closeAfter = true;
    this.flushing = true;
    this.flushTask = this.flush();
  }
  /** (internal) ends the connection now (server.close() on an idle one, closeAllConnections()). */
  _destroy(): void {
    this.state = 3;
    if (!this.closed) this.t.shutdown(2);
  }
  /** (internal) bytes to the client, in order: written now if the socket takes them, else by a task that waits.
   * false: more than the high-water mark is waiting. */
  send(b: Uint8Array): boolean {
    if (this.gone) return false;
    if (!this.flushing) {
      const w = this.t.send(b, 0);
      if (w < 0) {
        this.fail();
        return false;
      }
      if (w === b.length && !this.t.pending()) return true;
      if (w < b.length) {
        this.out.push(w === 0 ? b : b.subarray(w));
        this.queued += b.length - w;
      }
      this.flushing = true; // (all taken but not all written, as TLS records: the task writes them out)
      this.flushTask = this.flush();
      return this.queued < HIGH_WATER;
    }
    this.out.push(b);
    this.queued += b.length;
    return this.queued < HIGH_WATER;
  }
  private async flush(): Promise<void> {
    for (;;) {
      while (this.out.length > 0 && !this.gone) {
        const b = this.out[0];
        const r = this.t.writeAll(b, 0);
        this.out.shift();
        this.queued -= b.length;
        if (r < 0) this.fail();
      }
      if (this.gone || !this.t.pending()) break;
      if (this.t.writeAll(EMPTY, 0) < 0) this.fail(); // (what the transport still holds; more may be queued meanwhile)
    }
    this.out = [];
    this.queued = 0;
    this.flushing = false;
    this.drained();
  }
  /** Everything was written: the responses that ended finish, the current one may write more. */
  private drained(): void {
    if (this.finishing.length > 0) {
      const done = this.finishing;
      this.finishing = [];
      for (const r of done) {
        r._drained();
        if (this.gone) r._close();
        else r._finish(this.srv);
      }
    }
    const res = this.res;
    if (res !== null) res._drained();
    if (this.gone || !this.reading) {
      this.closeNow();
      return;
    }
    if (this.closeAfter) {
      this.closeAfter = false;
      if (!this.closed) this.endWriting();
    }
  }
  /** A write failed (the peer reset the connection): the reading wakes and ends too. */
  private fail(): void {
    if (this.gone) return;
    this.gone = true;
    if (!this.closed) this.t.shutdown(2);
  }
  /** The reading ended (the peer closed, or we did): a response not ended gets 'close'; the socket closes once no
   * write is in progress. */
  private lost(): void {
    this.gone = true;
    const msg = this.req;
    if (msg !== null && !msg.complete) msg.aborted = true;
    if (this.flushing) {
      this.t.shutdown(2); // (the waiting write wakes; drained() closes)
      return;
    }
    this.closeNow();
  }
  private closeNow(): void {
    if (this.closed || this.reading) {
      if (this.reading && !this.closed) this.t.shutdown(2);
      return;
    }
    this.closed = true;
    this.state = 3;
    const res = this.res;
    this.res = null;
    for (const r of this.finishing) r._close();
    this.finishing = [];
    if (res !== null) res._close();
    const msg = this.req;
    if (msg !== null) msg._close();
    this.t.close();
    this.sock._closed();
    this.srv._dropConnection(this);
  }
}

/** Node's http.Server: emits 'request' (req, res) for each request, 'connection', 'listening', 'close' and 'error'. */
export class Server extends EventEmitter {
  listening: boolean = false;
  /** how long an idle keep-alive connection stays open, ms (Node's default: 5 s) */
  keepAliveTimeout: int = 5000;
  headersTimeout: int = 60000;
  requestTimeout: int = 300000;
  timeout: int = 0;
  maxHeadersCount: int = 2000;
  protected port: int = 0;
  private host: string = "";
  private lfd: int = -1;
  private stopping: boolean = false;
  private conns: Map<int, HttpConnection> = new Map<int, HttpConnection>();
  private nextId: int = 1;
  private task: Promise<void> | null = null;
  private sweeper: int = -1;
  private closeEmitted: boolean = false;

  constructor(listener: RequestListener | null = null) {
    super();
    if (listener !== null) this.on("request", listener);
  }
  /** (internal) the socket of an accepted connection: node:https wraps it in TLS. */
  protected wrap(fd: int): HttpTransport {
    return new HttpTransport(fd);
  }
  /** (internal) true once close() was called */
  closing(): boolean {
    return this.stopping;
  }
  /** Starts listening on the port (0: any free port), on every address or the host given; the callback runs on
   * 'listening'. listen(cb), listen(port, cb), listen(port, host, cb), listen(port, host, backlog, cb) (the backlog
   * is the system's). */
  listen(port: number | string | (() => void) | null = 0, hostOrCallback: string | number | (() => void) | null = null,
         backlogOrCallback: number | (() => void) | null = null, callback: (() => void) | null = null): Server {
    let host = "";
    let cb: (() => void) | null = callback;
    let p: int = 0;
    if (typeof port === "number") p = Math.trunc(port as number);
    else if (typeof port === "string") p = Math.trunc(Number(port as string));
    else if (port !== null) cb = port as () => void;
    if (typeof hostOrCallback === "string") host = hostOrCallback as string;
    else if (typeof hostOrCallback === "function") cb = hostOrCallback as () => void;
    if (typeof backlogOrCallback === "function") cb = backlogOrCallback as () => void;
    if (cb !== null) this.once("listening", cb as () => void);
    const fd = net_listen(host === "localhost" ? "127.0.0.1" : host, p);
    if (fd < 0) {
      const code = net_code(fd);
      setImmediate(() => {
        this.emit("error", new HttpSystemError(code, "listen", ` ${host.length > 0 ? host : "::"}:${p}`));
      });
      return this;
    }
    this.lfd = fd;
    this.host = host;
    this.port = net_port(fd, false);
    this.listening = true;
    this.stopping = false;
    this.closeEmitted = false;
    setImmediate(() => {
      this.emit("listening");
    });
    this.task = this.acceptAll();
    this.sweeper = setInterval(() => {
      this.sweep();
    }, 1000);
    return this;
  }
  private async acceptAll(): Promise<void> {
    const lfd = this.lfd;
    for (;;) {
      const fd = net_accept(lfd);
      if (this.stopping) {
        if (fd >= 0) net_close(fd);
        break;
      }
      if (fd < 0) continue;
      const c = new HttpConnection(this, this.wrap(fd), this.nextId);
      this.nextId++;
      this.conns.set(c.id, c);
      this.emit("connection", c.sock);
      c.task = c.run();
    }
    net_close(lfd);
    if (this.lfd === lfd) this.lfd = -1;
    this.maybeClosed();
  }
  /** Ends the keep-alive connections idle for longer than keepAliveTimeout. */
  private sweep(): void {
    if (this.keepAliveTimeout <= 0) return;
    const now = Date.now();
    for (const c of this.conns.values()) if (c.idle() && now - c.idleSince > this.keepAliveTimeout) c._destroy();
  }
  /** (internal) a connection closed. */
  _dropConnection(c: HttpConnection): void {
    this.conns.delete(c.id);
    this.maybeClosed();
  }
  private maybeClosed(): void {
    if (this.stopping && this.lfd < 0 && this.conns.size === 0 && !this.closeEmitted) {
      this.closeEmitted = true;
      this.emit("close");
    }
  }
  /** Stops taking connections and ends the idle ones; 'close' (and the callback) once the requests in progress are
   * answered and their connections closed. The other servers of the program go on. */
  close(callback: (() => void) | null = null): Server {
    if (callback !== null) this.once("close", callback);
    if (this.stopping || this.lfd < 0) return this;
    this.stopping = true;
    this.listening = false;
    if (this.sweeper >= 0) {
      clearInterval(this.sweeper);
      this.sweeper = -1;
    }
    net_wake(this.port); // (the accepting task wakes, sees the server closing and stops)
    this.closeIdleConnections();
    return this;
  }
  /** Ends every connection that has no request in progress. */
  closeIdleConnections(): void {
    for (const c of this.conns.values()) if (c.idle()) c._destroy();
  }
  /** Ends every connection, requests in progress included. */
  closeAllConnections(): void {
    for (const c of this.conns.values()) c._destroy();
  }
  setTimeout(ms: int, callback: (() => void) | null = null): Server {
    this.timeout = ms;
    return this;
  }
  /** The address it listens on: "::" (every address) or the host given, and the port. */
  address(): AddressInfo | null {
    if (!this.listening) return null;
    if (this.host.length > 0) return new AddressInfo(this.host === "localhost" ? "127.0.0.1" : this.host, this.host.includes(":") ? "IPv6" : "IPv4", this.port);
    return new AddressInfo("::", "IPv6", this.port);
  }
  /** The number of open connections, to the callback. */
  getConnections(callback: (err: Error | null, count: int) => void): Server {
    const n = this.conns.size;
    setImmediate(() => {
      callback(null, n);
    });
    return this;
  }
  ref(): Server {
    return this;
  }
  unref(): Server {
    return this;
  }
}

/** (the name node:https and older code use) */
export type HttpServer = Server;

/** A server that calls the listener for each request. */
export function createServer(listener: RequestListener | null = null): Server {
  return new Server(listener);
}

/** The options of request()/get(). */
export interface RequestOptions {
  protocol?: string;
  hostname?: string;
  host?: string;
  port?: number | string;
  path?: string;
  method?: string;
  headers?: Record<string, string>;
  timeout?: number;
  /** (node:https) a CA certificate to trust, PEM */
  ca?: string | Uint8Array;
  /** (node:https) the name the certificate must have (SNI), when it is not the host's */
  servername?: string;
  /** the Agent whose connections the request uses (false: a connection of its own, closed after it); by default
   * the module's globalAgent */
  agent?: Agent | boolean | null;
}

/** new Agent(options): keepAlive (reuse connections), keepAliveMsecs, maxSockets (per host), maxTotalSockets,
 * maxFreeSockets, timeout (ms an idle connection is kept), scheduling ("lifo" or "fifo"). */
export interface AgentOptions {
  keepAlive?: boolean;
  keepAliveMsecs?: number;
  maxSockets?: number;
  maxTotalSockets?: number;
  maxFreeSockets?: number;
  timeout?: number;
  scheduling?: string;
}

/** What Agent.getName() reads: the host, the port, the local address and the family. */
export interface AgentNameOptions {
  host?: string;
  port?: number | string;
  localAddress?: string;
  family?: number;
}

// an idle connection in an Agent's pool, and since when (ms)
class FreeConn {
  t: HttpTransport;
  since: number;
  constructor(t: HttpTransport) {
    this.t = t;
    this.since = Date.now();
  }
}

/** (internal) what a ClientRequest asks of its Agent, as functions the Agent made: a request does not name the
 * Agent's methods, so a program that makes no client request (a server) carries none of the Agent's code. */
export class __PoolOps {
  owner: EventEmitter;
  keep: () => boolean;
  full: (name: string) => boolean;
  slot: (name: string) => Promise<void>;
  take: (name: string) => void;
  handed: (name: string) => HttpTransport | null;
  reuse: (name: string) => HttpTransport | null;
  done: (name: string, t: HttpTransport | null, reusable: boolean) => void;
  constructor(owner: EventEmitter, keep: () => boolean, full: (name: string) => boolean, slot: (name: string) => Promise<void>,
    take: (name: string) => void, handed: (name: string) => HttpTransport | null, reuse: (name: string) => HttpTransport | null,
    done: (name: string, t: HttpTransport | null, reusable: boolean) => void) {
    this.owner = owner;
    this.keep = keep;
    this.full = full;
    this.slot = slot;
    this.take = take;
    this.handed = handed;
    this.reuse = reuse;
    this.done = done;
  }
}

/** Node's http.Agent: the connections of the requests made with it. With keepAlive, a connection whose response
 * ended goes back to the agent and the next request to the same host and port takes it (req.reusedSocket); the
 * request says "Connection: keep-alive" (else "Connection: close", and each request has its own connection).
 * maxSockets limits the connections per host at a time (the other requests wait), maxTotalSockets all of them;
 * maxFreeSockets the idle ones kept per host. An idle connection does not keep the program alive, and one the
 * server closed is not reused. */
export class Agent extends EventEmitter {
  keepAlive: boolean = false;
  keepAliveMsecs: number = 1000;
  maxSockets: number = Infinity;
  maxFreeSockets: number = 256;
  maxTotalSockets: number = Infinity;
  scheduling: string = "lifo";
  timeout: number = 0;
  defaultPort: int = 80;
  protocol: string = "http:";
  private free: Map<string, FreeConn[]> = new Map<string, FreeConn[]>();
  private busy: Map<string, int> = new Map<string, int>();
  private waiting: (() => void)[] = [];
  private waitingFor: string[] = [];
  private handed: Map<string, HttpTransport[]> = new Map<string, HttpTransport[]>();
  private nbusy: int = 0;
  private nfree: int = 0;

  /** (keepAliveGlobal: the module's globalAgent, keep-alive with a 5 s idle timeout, as Node's) */
  constructor(options: AgentOptions | null = null, keepAliveGlobal: boolean = false) {
    super();
    if (keepAliveGlobal) {
      this.keepAlive = true;
      this.timeout = 5000;
    }
    if (options !== null) {
      this.keepAlive = options.keepAlive ?? false;
      this.keepAliveMsecs = options.keepAliveMsecs ?? 1000;
      this.maxSockets = options.maxSockets ?? Infinity;
      this.maxFreeSockets = options.maxFreeSockets ?? 256;
      this.maxTotalSockets = options.maxTotalSockets ?? Infinity;
      this.scheduling = options.scheduling ?? "lifo";
      this.timeout = options.timeout ?? 0;
    }
  }
  /** The connections it has now, in use and idle. */
  get totalSocketCount(): int {
    return this.nbusy + this.nfree;
  }
  /** The key of a host's connections: "host:port:localAddress" (and ":family" for 4 or 6). */
  getName(options: AgentNameOptions | null = null): string {
    let name = "localhost";
    if (options !== null) {
      const h = options.host ?? "";
      if (h.length > 0) name = h;
    }
    name += ":";
    if (options !== null) {
      const port = options.port;
      if (typeof port === "number") {
        if ((port as number) !== 0) name += String(port as number);
      } else if (typeof port === "string") name += port as string;
    }
    name += ":";
    if (options !== null) {
      name += options.localAddress ?? "";
      const fam = options.family ?? 0;
      if (fam === 4 || fam === 6) name += ":" + String(fam);
    }
    return name;
  }
  /** Closes its idle connections (the ones in use close when their requests end); they leave totalSocketCount on the
   * next tick, as their 'close' events do in Node. */
  destroy(): void {
    let n: int = 0;
    for (const list of this.free.values()) {
      for (const c of list) c.t.close();
      n += list.length;
    }
    this.free.clear();
    process.nextTick((): void => {
      this.nfree -= n;
    });
  }
  private busyOf(name: string): int {
    return this.busy.get(name) ?? 0;
  }
  // a request may open (or take) a connection now: below maxSockets for its host and maxTotalSockets in all
  private room(name: string): boolean {
    return this.busyOf(name) < this.maxSockets && this.nbusy < this.maxTotalSockets;
  }
  _keep(): boolean {
    return this.keepAlive;
  }
  private ops: __PoolOps | null = null;
  /** (internal) its functions for the requests that use it */
  _ops(): __PoolOps {
    const o = this.ops;
    if (o !== null) return o;
    const made = new __PoolOps(this, (): boolean => this._keep(), (n: string): boolean => this._full(n), (n: string): Promise<void> => this._slot(n),
      (n: string): void => this._take(n), (n: string): HttpTransport | null => this._handed(n), (n: string): HttpTransport | null => this._reuse(n),
      (n: string, t: HttpTransport | null, r: boolean): void => this._done(n, t, r));
    this.ops = made;
    return made;
  }
  /** (internal) waits until the request may have a connection of this host, then counts it in use. */
  async _slot(name: string): Promise<void> {
    while (!this.room(name)) {
      await new Promise<void>((resolve) => {
        this.waiting.push(resolve);
        this.waitingFor.push(name);
      });
    }
    this.busy.set(name, this.busyOf(name) + 1);
    this.nbusy++;
  }
  /** (internal) whether the request has to wait for a connection (maxSockets reached). */
  _full(name: string): boolean {
    return !this.room(name);
  }
  /** (internal) counts a connection in use without waiting (there is room). */
  _take(name: string): void {
    this.busy.set(name, this.busyOf(name) + 1);
    this.nbusy++;
  }
  /** (internal) an idle connection of this host to use again, or null (the stale ones are closed). */
  _reuse(name: string): HttpTransport | null {
    if (!this.keepAlive) return null;
    const list = this.free.get(name);
    if (list === null) return null;
    while (list.length > 0) {
      const c = this.scheduling === "fifo" ? (list.shift() as FreeConn) : (list.pop() as FreeConn);
      this.nfree--;
      const old = this.timeout > 0 && Date.now() - c.since > this.timeout;
      if (!old && c.t.alive()) return c.t;
      c.t.close();
    }
    return null;
  }
  /** (internal) the connection a request that waited was given (as Node hands a socket to a queued request: not
   * counted as reused), or null. */
  _handed(name: string): HttpTransport | null {
    const list = this.handed.get(name);
    if (list === null || list.length === 0) return null;
    this.nfree--;
    return list.shift() as HttpTransport;
  }
  private waits(name: string): boolean {
    for (const w of this.waitingFor) if (w === name) return true;
    return false;
  }
  /** (internal) the request is done with its connection: given to a request waiting for this host, kept for the next
   * one (keepAlive, reusable) or closed. */
  _done(name: string, t: HttpTransport | null, reusable: boolean): void {
    this.busy.set(name, this.busyOf(name) - 1);
    this.nbusy--;
    if (t !== null && reusable && this.keepAlive && this.waits(name)) {
      const found = this.handed.get(name);
      const list: HttpTransport[] = found !== null ? found : [];
      if (found === null) this.handed.set(name, list);
      list.push(t);
      this.nfree++;
    } else if (t !== null) {
      const found = this.free.get(name);
      const list: FreeConn[] = found !== null ? found : [];
      if (reusable && this.keepAlive && list.length < this.maxFreeSockets) {
        if (found === null) this.free.set(name, list);
        list.push(new FreeConn(t));
        this.nfree++;
      } else {
        t.close();
      }
    }
    // the first request waiting for a connection that there is room for now
    for (let i = 0; i < this.waiting.length; i++) {
      if (this.room(this.waitingFor[i])) {
        const w = this.waiting[i];
        this.waiting.splice(i, 1);
        this.waitingFor.splice(i, 1);
        w();
        break;
      }
    }
  }
}

/** The Agent of the requests that name none: keepAlive, as Node's since v19. */
// (a `new` alone: the compiler makes it only in a program that reads globalAgent, src/prune.c lazy_var; an options
// object here would be compiled into every program that loads the module)
export let globalAgent: Agent = new Agent(null, true);

export type ResponseListener = (res: IncomingMessage) => void;

/** A request being made: write() a body, then end() sends it; 'response' gives the IncomingMessage, whose body
 * comes as it arrives. */
export class ClientRequest extends Writable {
  method: string;
  path: string;
  host: string;
  protocol: string;
  /** the port, "" when the URL gives none (for node:diagnostics_channel's subscribers) */
  port: string = "";
  /** the name the server's certificate must have (node:https) */
  servername: string = "";
  /** for node:diagnostics_channel's subscribers (an APM agent's span for this request) */
  _diag: any = null;
  reusedSocket: boolean = false;
  aborted: boolean = false;
  /** (internal) the Agent whose connections it uses (null: agent: false, a connection of its own) */
  _pool: __PoolOps | null = null;
  private killed: boolean = false;
  /** the request's socket, from 'socket' on (null before): socket.unref() lets the program end while it is in flight
   * (an APM agent's send). Only ref()/unref() act on it: the connection itself is the request's HttpTransport. */
  socket: Socket | null = null;
  private url: string;
  private headers: Map<string, string> = new Map<string, string>(); // name as set -> value
  private chunks: Uint8Array[] = [];
  private ended: boolean = false;
  private t: HttpTransport | null = null;
  private task: Promise<void> | null = null;
  private timer: int = -1;
  private gotResponse: boolean = false;
  private agentKey: string = "";
  /** The Agent whose connections it uses (null: agent: false). */
  get agent(): Agent | null {
    const a = this._pool;
    return a !== null ? (a.owner as Agent) : null;
  }

  constructor(url: string, method: string) {
    super(null);
    this.url = url;
    this.method = method;
    const u = new URL(url);
    this.path = u.pathname + u.search;
    this.host = u.hostname;
    this.protocol = u.protocol;
    this.port = u.port;
    if (u.protocol !== this.expected()) throw new TypeError(`Protocol "${u.protocol}" not supported. Expected "${this.expected()}"`);
  }
  /** (internal) the protocol this module speaks (node:https: "https:"). */
  protected expected(): string {
    return "http:";
  }
  /** (internal) the port when the URL gives none. */
  protected defaultPort(): int {
    return 80;
  }
  /** (internal) a connection to the server: node:https's is TLS. Throws an HttpSystemError when it cannot be made. */
  protected open(host: string, port: int): HttpTransport {
    const fd = net_connect(host, port);
    if (fd < 0) {
      const code = net_code(fd);
      throw new HttpSystemError(code, code === "ENOTFOUND" ? "getaddrinfo" : "connect", code === "ENOTFOUND" ? ` ${host}` : ` ${host === "localhost" ? "127.0.0.1" : host}:${port}`);
    }
    return new HttpTransport(fd);
  }
  setHeader(name: string, value: string | number | string[]): ClientRequest {
    const lc = name.toLowerCase();
    for (const k of this.headers.keys()) {
      if (k.toLowerCase() === lc) {
        this.headers.delete(k);
        break;
      }
    }
    this.headers.set(name, Array.isArray(value) ? (value as string[]).join(", ") : String(value));
    return this;
  }
  getHeader(name: string): string | null {
    for (const k of this.headers.keys()) if (k.toLowerCase() === name.toLowerCase()) return this.headers.get(k);
    return null;
  }
  hasHeader(name: string): boolean {
    return this.getHeader(name) !== null;
  }
  removeHeader(name: string): void {
    for (const k of this.headers.keys()) {
      if (k.toLowerCase() === name.toLowerCase()) {
        this.headers.delete(k);
        return;
      }
    }
  }
  getHeaderNames(): string[] {
    const out: string[] = [];
    for (const k of this.headers.keys()) out.push(k.toLowerCase());
    return out;
  }
  /** 'timeout' after ms without the response complete (the callback is a 'timeout' listener). */
  setTimeout(ms: int, callback: (() => void) | null = null): ClientRequest {
    if (callback !== null) this.once("timeout", callback);
    if (this.timer >= 0) clearTimeout(this.timer);
    this.timer = setTimeout(() => {
      this.timer = -1;
      this.emit("timeout");
    }, ms);
    return this;
  }
  /** Adds to the body (sent with end()). */
  override write(chunk: any, encoding: string | ErrorCallback | null = null, cb: ErrorCallback | null = null): boolean {
    let enc: string | null = null;
    let f: ErrorCallback | null = cb;
    if (typeof encoding === "string") enc = encoding as string;
    else if (encoding !== null) f = encoding as ErrorCallback;
    if (this.ended) throw new Error("write after end");
    this.chunks.push(toBytes(chunk, enc));
    if (f !== null) {
      const g = f as ErrorCallback;
      process.nextTick(() => g(null));
    }
    return true;
  }
  /** Sends the request; the response comes as 'response'. */
  override end(chunk: any = null, encoding: string | ErrorCallback | null = null, cb: ErrorCallback | null = null): ClientRequest {
    let c: any = chunk;
    let enc: string | null = null;
    let f: ErrorCallback | null = cb;
    if (typeof encoding === "string") enc = encoding as string;
    else if (encoding !== null) f = encoding as ErrorCallback;
    if (c !== null && typeof c === "function") {
      f = c as ErrorCallback;
      c = null;
    }
    if (this.ended) return this;
    if (c !== null) this.chunks.push(toBytes(c, enc));
    this.ended = true;
    const ws = this._writableState;
    if (ws !== null) {
      ws.ending = true;
      ws.ended = true;
    }
    if (f !== null) {
      const g = f as ErrorCallback;
      this.once("finish", () => g(null));
    }
    setImmediate(() => {
      this.task = this.send();
    });
    return this;
  }
  /** Ends the request at once: the connection is closed ('close'; 'error' "socket hang up" before a response). */
  override destroy(err: Error | null = null, cb: ErrorCallback | null = null): ClientRequest {
    if (this.killed) return this;
    this.killed = true;
    const t = this.t;
    if (t !== null) t.shutdown(2);
    if (err !== null) this.emit("error", err);
    return this;
  }
  abort(): void {
    this.aborted = true;
    this.destroy();
  }
  private fail(err: Error): void {
    const errCh = channel("http.client.request.error");
    if (errCh.hasSubscribers) {
      const m = new HttpChannelMessage(this);
      m.error = err;
      errCh.publish(m);
    }
    this.emit("error", err);
    this.done();
  }
  // the connection after the exchange: back to the agent (kept when reusable), or closed
  private release(t: HttpTransport, reusable: boolean): void {
    const agent = this._pool;
    if (agent !== null) agent.done(this.agentKey, t, reusable);
    else t.close();
    this.t = null;
  }
  private done(): void {
    if (this.timer >= 0) {
      clearTimeout(this.timer);
      this.timer = -1;
    }
    this.emit("close");
  }
  /** The request's task: the socket's I/O (its unref()/ref() act on this task) until the response's body ended. */
  private async send(): Promise<void> {
    const sock = new Socket();
    this.socket = sock;
    sock._bindTask();
    this.emit("socket", sock);
    await this.exchange();
    sock._unbindTask();
  }
  private async exchange(): Promise<void> {
    const startCh = channel("http.client.request.start");
    if (startCh.hasSubscribers) startCh.publish(new HttpChannelMessage(this)); // (a subscriber may setHeader())
    if (this.killed) {
      this.fail(new HttpSystemError("ECONNRESET", "socket", " hang up"));
      return;
    }
    const port = this.port.length > 0 ? Math.trunc(Number(this.port)) : this.defaultPort();
    const agent = this._pool;
    const key = this.host + ":" + String(port) + ":";
    let handed: HttpTransport | null = null;
    if (agent !== null) {
      if (agent.full(key)) {
        await agent.slot(key);
        handed = agent.handed(key);
      } else agent.take(key);
    }
    let t: HttpTransport;
    const reused = agent !== null && handed === null ? agent.reuse(key) : null;
    if (handed !== null) {
      t = handed;
    } else if (reused !== null) {
      t = reused;
      this.reusedSocket = true;
    } else {
      try {
        t = this.open(this.host, port);
      } catch (e) {
        if (agent !== null) agent.done(key, null, false);
        this.fail(e as Error);
        return;
      }
    }
    this.t = t;
    this.agentKey = key;
    const body = this.chunks.length === 1 ? this.chunks[0] : concat(this.chunks);
    this.chunks = [];
    let head = `${this.method} ${this.path.length > 0 ? this.path : "/"} HTTP/1.1\r\n`;
    let hasHost = false;
    let hasConn = false;
    let hasLen = false;
    for (const k of this.headers.keys()) {
      const lc = k.toLowerCase();
      if (lc === "host") hasHost = true;
      if (lc === "connection") hasConn = true;
      if (lc === "content-length" || lc === "transfer-encoding") hasLen = true;
    }
    if (!hasHost) head += "Host: " + this.host + (this.port.length > 0 ? ":" + this.port : "") + "\r\n";
    for (const k of this.headers.keys()) head += k + ": " + (this.headers.get(k) ?? "") + "\r\n";
    const m = this.method;
    if (!hasLen && (body.length > 0 || !(m === "GET" || m === "HEAD" || m === "DELETE" || m === "OPTIONS" || m === "TRACE" || m === "CONNECT"))) head += "Content-Length: " + String(body.length) + "\r\n";
    if (!hasConn) head += agent !== null && agent.keep() ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    head += "\r\n";
    const out = body.length > 0 ? concat([Buffer.from(head), body]) : Buffer.from(head);
    if (t.writeAll(out, 0) < 0 && !this.killed) {
      this.release(t, false);
      this.fail(new HttpSystemError("EPIPE", "write", ""));
      return;
    }
    const ws = this._writableState;
    if (ws !== null) ws.finished = true;
    this.emit("finish");
    await this.receive(t);
  }
  /** Reads the response: its head, then its body as it arrives (Content-Length, chunked, or up to the end). */
  private async receive(t: HttpTransport): Promise<void> {
    let buf: Uint8Array = EMPTY;
    let pos: int = 0;
    let parts: string[] = [];
    for (;;) {
      if (buf.length > pos) {
        parts = nhttp_parse(buf, pos, buf.length);
        const end = nhttp_end();
        if (end === -2) break;
        if (end >= 0) {
          pos = end;
          const code = parts.length >= 2 ? Math.trunc(Number(parts[1])) : 0;
          if (code >= 100 && code < 200 && code !== 101) { // (100 Continue and the other interim answers: the real one follows)
            parts = [];
            continue;
          }
          break;
        }
      }
      const chunk = t.read();
      if (chunk.length === 0) break;
      buf = buf.length > pos ? concat([buf.subarray(pos), chunk]) : chunk;
      pos = 0;
    }
    if (parts.length < 3) {
      this.release(t, false);
      this.fail(new HttpSystemError("ECONNRESET", "socket", " hang up"));
      return;
    }
    const msg = new IncomingMessage();
    const sock = new HttpSocket(t);
    msg.socket = sock;
    msg._setVersion(parts[0]);
    msg.statusCode = Math.trunc(Number(parts[1]));
    msg.statusMessage = parts[2].length > 0 ? parts[2] : reason(msg.statusCode);
    msg._setHeaders(parts);
    this.gotResponse = true;
    const finCh = channel("http.client.response.finish");
    if (finCh.hasSubscribers) {
      const dm = new HttpChannelMessage(this);
      dm.response = msg;
      finCh.publish(dm);
    }
    this.emit("response", msg);
    const te = msg.headers["transfer-encoding"];
    const cl = msg.headers["content-length"];
    const code = msg.statusCode;
    let ok = true;
    let delimited = true; // (the body's end is known: the connection can carry another request)
    if (this.method === "HEAD" || code === 204 || code === 304) {
      // (no body)
    } else if (te !== null && te.toLowerCase().includes("chunked")) {
      ok = await this.chunkedBody(t, msg, buf, pos);
    } else if (cl !== null) {
      let left = Math.trunc(Number(cl));
      if (buf.length > pos && left > 0) {
        const n = buf.length - pos < left ? buf.length - pos : left;
        msg._data(buf.subarray(pos, pos + n));
        left -= n;
      }
      while (left > 0) {
        const chunk = t.read();
        if (chunk.length === 0) {
          ok = false;
          break;
        }
        const n = chunk.length < left ? chunk.length : left;
        msg._data(n === chunk.length ? chunk : chunk.subarray(0, n));
        left -= n;
      }
    } else {
      delimited = false;
      if (buf.length > pos) msg._data(buf.subarray(pos));
      for (;;) {
        const chunk = t.read();
        if (chunk.length === 0) break;
        msg._data(chunk);
      }
    }
    const conn = msg.headers["connection"];
    const keep = ok && delimited && !this.killed && parts[0] !== "1.0" && !(conn !== null && conn.toLowerCase().includes("close"));
    this.release(t, keep);
    sock._closed();
    if (ok) msg._end();
    else {
      msg.aborted = true;
      msg.emit("aborted");
      // (cut by the server: 'error' for whoever listens; by destroy(): only 'aborted' and 'close', as Node)
      if (!this.killed && msg.listenerCount("error") > 0) msg.emit("error", new HttpSystemError("ECONNRESET", "aborted", ""));
    }
    msg._close();
    this.done();
  }
  private async chunkedBody(t: HttpTransport, msg: IncomingMessage, buf0: Uint8Array, pos0: int): Promise<boolean> {
    let buf = buf0;
    let pos = pos0;
    let left: int = -1; // -1: a size line; > 0: data; -3: the CRLF after it; -2: trailers
    for (;;) {
      const avail = buf.length - pos;
      let need = false;
      if (left > 0) {
        if (avail === 0) need = true;
        else {
          const n = avail < left ? avail : left;
          msg._data(buf.subarray(pos, pos + n));
          pos += n;
          left -= n;
          if (left === 0) left = -3;
        }
      } else if (left === -3) {
        if (avail < 2) need = true;
        else {
          pos += 2;
          left = -1;
        }
      } else {
        const nl = buf.indexOf(10, pos);
        if (nl < 0) need = true;
        else {
          const line = Buffer.from(buf.subarray(pos, nl)).toString("latin1").trim();
          pos = nl + 1;
          if (left === -2) {
            if (line.length === 0) return true;
            const colon = line.indexOf(":");
            if (colon > 0) {
              msg.rawTrailers.push(line.slice(0, colon).trim());
              msg.rawTrailers.push(line.slice(colon + 1).trim());
              msg.trailers[line.slice(0, colon).trim().toLowerCase()] = line.slice(colon + 1).trim();
            }
          } else {
            const semi = line.indexOf(";");
            const size = hexSize((semi >= 0 ? line.slice(0, semi) : line).trim());
            if (size < 0) return false;
            left = size === 0 ? -2 : Math.trunc(size);
          }
        }
      }
      if (need) {
        const chunk = t.read();
        if (chunk.length === 0) return false;
        buf = buf.length > pos ? concat([buf.subarray(pos), chunk]) : chunk;
        pos = 0;
      }
    }
  }
}

/** A chunk's size line: its hex digits only (parseInt would take "1g" as 1), -1 when it is not one. */
function hexSize(hex: string): number {
  if (hex.length === 0 || hex.length > 13) return -1;
  for (let i = 0; i < hex.length; i++) {
    const c = hex.charCodeAt(i);
    if (!((c >= 48 && c <= 57) || (c >= 65 && c <= 70) || (c >= 97 && c <= 102))) return -1;
  }
  return parseInt(hex, 16);
}

/** (internal) the URL of a request()'s target. */
export function _toUrl(target: string | URL | RequestOptions, defaultProtocol: string): string {
  if (typeof target === "string") return target as string;
  if (target instanceof URL) return (target as URL).href;
  const o = target as RequestOptions;
  const protocol = o.protocol ?? defaultProtocol;
  let host = o.hostname ?? o.host ?? "localhost";
  const op = o.port;
  let port: int = op === null ? 0 : typeof op === "string" ? Math.trunc(Number(op as string)) : Math.trunc(op as number);
  if (o.hostname === null && o.host !== null) { // (host may carry the port: "example.com:8080")
    const colon = host.lastIndexOf(":");
    if (colon > 0 && !host.includes("]") && host.indexOf(":") === colon) {
      if (port === 0) port = Math.trunc(Number(host.slice(colon + 1)));
      host = host.slice(0, colon);
    }
  }
  const path = o.path ?? "/";
  return `${protocol}//${host}${port > 0 ? ":" + String(port) : ""}${path}`;
}

/** (internal) the method and headers of request(), given to a ClientRequest made by http or https. */
export function _setup(req: ClientRequest, target: string | URL | RequestOptions, callback: ResponseListener | null, defaultAgent: Agent | null = null): ClientRequest {
  const opts: RequestOptions | null = typeof target === "string" || target instanceof URL ? null : (target as RequestOptions);
  req._pool = defaultAgent !== null ? defaultAgent._ops() : null;
  if (opts !== null) {
    const a = opts.agent;
    if (a instanceof Agent) req._pool = (a as Agent)._ops();
    else if (a === false) req._pool = null;
  }
  if (opts !== null && opts.headers !== null) {
    const hs = opts.headers as Record<string, string>;
    for (const k of Object.keys(hs)) req.setHeader(k, hs[k] ?? "");
  }
  if (opts !== null && opts.timeout !== null && (opts.timeout as number) > 0) req.setTimeout(Math.trunc(opts.timeout as number));
  if (opts !== null && opts.servername !== null) req.servername = opts.servername as string;
  if (callback !== null) req.once("response", callback);
  return req;
}

/** (internal) the method request() uses. */
export function _method(target: string | URL | RequestOptions): string {
  const opts: RequestOptions | null = typeof target === "string" || target instanceof URL ? null : (target as RequestOptions);
  return opts !== null && opts.method !== null ? (opts.method as string).toUpperCase() : "GET";
}

/** Makes a request: call end() (with a body) to send it. */
export function request(target: string | URL | RequestOptions, callback: ResponseListener | null = null): ClientRequest {
  return _setup(new ClientRequest(_toUrl(target, "http:"), _method(target)), target, callback, globalAgent);
}

/** A GET request, sent at once. */
export function get(target: string | URL | RequestOptions, callback: ResponseListener | null = null): ClientRequest {
  const req = request(target, callback);
  req.end();
  return req;
}
