// xisto:http (node:http): createServer(listener) with IncomingMessage and ServerResponse, server.listen/close/address,
// and the client request()/get(), over xisto's own HTTP server (the built-in Server) and fetch.
// listen() binds at once and returns; the server answers once the program's own code is done (its event loop), as
// in Node. Differences from Node (docs/docs/guides/node-compatibility.md): bodies are text (binary request and
// response bodies are read and written as UTF-8); a response is sent when end() is called (write() buffers);
// one server per program. IncomingMessage is a stream.Readable (data, end, pipe, for await).
import { EventEmitter, Listener } from "xisto:events";
import { Readable } from "xisto:stream";

declare function http_bind(s: Server, port: int): int;
declare function http_close(): void;

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

function reason(code: int): string {
  const k = String(code);
  return STATUS_CODES[k] ?? "unknown";
}

/** A request the server received, or a response the client got: status/method, headers and the body as events. */
export class IncomingMessage extends Readable {
  method: string = "";
  url: string = "";
  statusCode: int = 0;
  statusMessage: string = "";
  httpVersion: string = "1.1";
  httpVersionMajor: int = 1;
  httpVersionMinor: int = 1;
  /** The headers by lower-case name (several of one name joined with ", "). */
  headers: Record<string, string> = {};
  /** The headers as they came: name, value, name, value... */
  rawHeaders: string[] = [];
  complete: boolean = false;
  private body: string = "";
  private fed: boolean = false;

  /** (the body is pushed from outside: nothing to read on demand) */
  _read(size: number): void {}
  /** (internal) the body, given once. */
  _setBody(body: string): void {
    this.body = body;
  }
  /** (internal) pushes the body, then its end: a stream.Readable as Node's (data, end, pipe, for await). */
  _flow(): void {
    if (this.fed) return;
    this.fed = true;
    if (this.body.length > 0) this.push(Buffer.from(this.body));
    this.push(null);
    this.once("end", () => {
      this.complete = true;
    });
  }
}

/** The response a request listener writes: status, headers, then the body with write() and end(). */
export class ServerResponse extends EventEmitter {
  statusCode: int = 200;
  statusMessage: string = "";
  headersSent: boolean = false;
  writableEnded: boolean = false;
  writableFinished: boolean = false;
  sendDate: boolean = true;
  private names: Map<string, string> = new Map<string, string>(); // lower-case name -> name as set
  private values: Map<string, string[]> = new Map<string, string[]>();
  private chunks: string[] = [];
  private done: ((code: int) => void) | null = null;

  /** Sets a header (replacing one of the same name, in any case). */
  setHeader(name: string, value: string | number | string[]): ServerResponse {
    if (this.headersSent) throw new Error("Cannot set headers after they are sent to the client");
    const k = name.toLowerCase();
    this.names.set(k, name);
    if (Array.isArray(value)) this.values.set(k, value as string[]);
    else this.values.set(k, [String(value)]);
    return this;
  }
  /** Adds a value to a header (set-cookie: one line each). */
  appendHeader(name: string, value: string | string[]): ServerResponse {
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
    const k = name.toLowerCase();
    this.values.delete(k);
    this.names.delete(k);
  }
  /** Sets the status (and its reason phrase) and headers at once. */
  writeHead(statusCode: int, headers: Record<string, string> | null = null): ServerResponse {
    this.statusCode = statusCode;
    if (headers !== null) for (const k of Object.keys(headers)) this.setHeader(k, headers[k] ?? "");
    this.headersSent = true;
    return this;
  }
  /** Adds to the body (text, or bytes read as UTF-8). Returns true: nothing waits for a slow client. */
  write(chunk: string | Uint8Array): boolean {
    if (this.writableEnded) throw new Error("write after end");
    this.headersSent = true;
    this.chunks.push(typeof chunk === "string" ? (chunk as string) : new TextDecoder().decode(chunk as Uint8Array));
    return true;
  }
  /** Ends the response (with a last chunk); the server sends it. */
  end(chunk: string | Uint8Array | null = null): ServerResponse {
    if (this.writableEnded) return this;
    if (chunk !== null) this.write(chunk);
    this.writableEnded = true;
    this.headersSent = true;
    const d = this.done;
    this.done = null;
    if (d !== null) d(0);
    return this;
  }
  /** (internal) called when end() is. */
  _onDone(fn: (code: int) => void): void {
    if (this.writableEnded) fn(0);
    else this.done = fn;
  }
  /** (internal) the response to send. */
  _response(): HttpResponse {
    const ct = this.getHeader("content-type");
    let r = HttpResponse.of(this.statusCode, ct === null ? "" : ct, this.chunks.join(""));
    for (const k of this.values.keys()) {
      if (k === "content-type" || k === "content-length" || k === "transfer-encoding" || k === "connection") continue;
      for (const v of this.values.get(k) ?? []) r = r.header(this.names.get(k) ?? k, v);
    }
    return r;
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

let current: HttpServer | null = null;

async function answer(req: HttpRequest): Promise<HttpResponse> {
  const srv = current;
  const msg = new IncomingMessage();
  msg.method = req.method;
  const search = req.search();
  msg.url = search.length > 0 ? (search.startsWith("?") ? req.path + search : req.path + "?" + search) : req.path;
  for (const name of req.headerNames()) {
    const v = req.header(name);
    msg.rawHeaders.push(name);
    msg.rawHeaders.push(v);
    const k = name.toLowerCase();
    msg.headers[k] = k in msg.headers ? msg.headers[k] + ", " + v : v;
  }
  msg._setBody(req.body);
  const res = new ServerResponse();
  const finished = new Promise<int>((resolve) => {
    res._onDone(resolve);
  });
  msg._flow(); // (the body waits in the stream's buffer: an async listener that reads it runs within the emit)
  if (srv !== null) srv.emit("request", msg, res);
  await finished;
  res.writableFinished = true;
  setImmediate(() => {
    res.emit("finish");
    res.emit("close");
  });
  return res._response();
}

/** Node's http.Server: emits 'request' (req, res) for each request, 'listening' and 'close'. */
export class HttpServer extends EventEmitter {
  listening: boolean = false;
  protected port: int = 0;
  protected native: Server = new Server();

  constructor(listener: RequestListener | null = null) {
    super();
    if (listener !== null) this.on("request", listener);
    this.native.fallback(answer);
  }
  /** Starts listening on the port (0: any free port); the callback runs once it is. */
  /** (internal) binds the native server: node:https binds it with its certificate. */
  protected bind(native: Server, port: int): int {
    return http_bind(native, port);
  }
  listen(port: int = 0, callback: (() => void) | null = null): HttpServer {
    current = this;
    this.port = this.bind(this.native, port);
    this.listening = true;
    if (callback !== null) this.once("listening", callback);
    setImmediate(() => {
      this.emit("listening");
    });
    return this;
  }
  /** Stops taking connections; the callback runs once the requests in progress are answered. */
  close(callback: (() => void) | null = null): HttpServer {
    if (callback !== null) this.once("close", callback);
    http_close();
    this.listening = false;
    setImmediate(() => {
      this.emit("close");
    });
    return this;
  }
  address(): AddressInfo | null {
    return this.listening ? new AddressInfo("::", "IPv6", this.port) : null;
  }
}

/** A server that calls the listener for each request. */
export function createServer(listener: RequestListener | null = null): HttpServer {
  return new HttpServer(listener);
}

/** The options of request()/get(). */
export interface RequestOptions {
  protocol?: string;
  hostname?: string;
  host?: string;
  port?: int;
  path?: string;
  method?: string;
  headers?: Record<string, string>;
  timeout?: int;
}

export type ResponseListener = (res: IncomingMessage) => void;

/** A request being made: write() a body, then end() sends it; 'response' gives the IncomingMessage. */
export class ClientRequest extends EventEmitter {
  method: string;
  path: string;
  host: string;
  protocol: string;
  private url: string;
  private headers: Map<string, string> = new Map<string, string>();
  private chunks: string[] = [];
  private ended: boolean = false;

  constructor(url: string, method: string) {
    super();
    this.url = url;
    this.method = method;
    const u = new URL(url);
    this.path = u.pathname + u.search;
    this.host = u.hostname;
    this.protocol = u.protocol;
    if (u.protocol !== this.expected()) throw new TypeError(`Protocol "${u.protocol}" not supported. Expected "${this.expected()}"`);
  }
  /** (internal) the protocol this module speaks (node:https: "https:"). */
  protected expected(): string {
    return "http:";
  }
  /** (internal) the request itself: rest is the URL after "http://" (node:https fetches "https://" + rest). */
  protected async fetchRest(rest: string, withBody: boolean, h: Record<string, string>, body: string): Promise<Response> {
    // ("http://" written here: plain http links no TLS into the program; node:https is the one that does)
    if (!withBody) return await fetch("http://" + rest, { method: this.method, headers: h });
    return await fetch("http://" + rest, { method: this.method, headers: h, body: body });
  }
  setHeader(name: string, value: string | number): ClientRequest {
    this.headers.set(name, String(value));
    return this;
  }
  getHeader(name: string): string | null {
    for (const k of this.headers.keys()) if (k.toLowerCase() === name.toLowerCase()) return this.headers.get(k);
    return null;
  }
  write(chunk: string | Uint8Array): boolean {
    this.chunks.push(typeof chunk === "string" ? (chunk as string) : new TextDecoder().decode(chunk as Uint8Array));
    return true;
  }
  /** Sends the request; the response comes as 'response'. */
  end(chunk: string | Uint8Array | null = null): ClientRequest {
    if (this.ended) return this;
    if (chunk !== null) this.write(chunk);
    this.ended = true;
    setImmediate(() => {
      this.send();
    });
    return this;
  }
  private async send(): Promise<void> {
    const h: Record<string, string> = {};
    for (const k of this.headers.keys()) h[k] = this.headers.get(k) ?? "";
    const body = this.chunks.join("");
    try {
      const rest = this.url.slice(this.expected().length + 2);
      const r = await this.fetchRest(rest, !(this.method === "GET" || this.method === "HEAD"), h, body);
      const msg = new IncomingMessage();
      msg.statusCode = r.status;
      msg.statusMessage = reason(r.status);
      r.headers.forEach((v: string, k: string) => {
        msg.headers[k] = v;
        msg.rawHeaders.push(k);
        msg.rawHeaders.push(v);
      });
      msg._setBody(r.text());
      this.emit("response", msg);
      msg._flow();
    } catch (err) {
      this.emit("error", err as Error);
    }
  }
}

/** (internal) the URL of a request()'s target. */
export function _toUrl(target: string | RequestOptions, defaultProtocol: string): string {
  if (typeof target === "string") return target as string;
  const o = target as RequestOptions;
  const protocol = o.protocol ?? defaultProtocol;
  const host = o.hostname ?? o.host ?? "localhost";
  const port = o.port ?? 0;
  const path = o.path ?? "/";
  return `${protocol}//${host}${port > 0 ? ":" + String(port) : ""}${path}`;
}

/** (internal) the method and headers of request(), given to a ClientRequest made by http or https. */
export function _setup(req: ClientRequest, target: string | RequestOptions, callback: ResponseListener | null): ClientRequest {
  const opts: RequestOptions | null = typeof target === "string" ? null : (target as RequestOptions);
  if (opts !== null && opts.headers !== null) {
    const hs = opts.headers as Record<string, string>;
    for (const k of Object.keys(hs)) req.setHeader(k, hs[k] ?? "");
  }
  if (callback !== null) req.once("response", callback);
  return req;
}

/** (internal) the method request() uses. */
export function _method(target: string | RequestOptions): string {
  const opts: RequestOptions | null = typeof target === "string" ? null : (target as RequestOptions);
  return opts !== null && opts.method !== null ? (opts.method as string).toUpperCase() : "GET";
}

/** Makes a request: call end() (with a body) to send it. */
export function request(target: string | RequestOptions, callback: ResponseListener | null = null): ClientRequest {
  return _setup(new ClientRequest(_toUrl(target, "http:"), _method(target)), target, callback);
}

/** A GET request, sent at once. */
export function get(target: string | RequestOptions, callback: ResponseListener | null = null): ClientRequest {
  const req = request(target, callback);
  req.end();
  return req;
}

