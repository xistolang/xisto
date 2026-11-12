// The part of Express that request handlers, exception filters and middleware read and write. A framework (Nest)
// builds the Request from xisto's HttpRequest and turns the Response, once the handler is done, into the
// HttpResponse the server sends.
/**
 * The request's headers by lower-case name: headers.authorization, headers["x-forwarded-for"].
 * Compatibility: each value is the header's text (Node gives an array for set-cookie).
 */
export class IncomingHttpHeaders {
  authorization?: string;
  /** (the other headers: an open class, read by name) */
  __expando: Record<string, any> = {};
}

/**
 * The incoming request, as Express gives it to handlers.
 * Compatibility: what middleware and guards add to it (request.terminal = t) is kept by name, as in JavaScript.
 */
export class Request {
  method: string = "GET";
  url: string = "/";
  path: string = "/";
  headers: IncomingHttpHeaders = new IncomingHttpHeaders();
  body: string = "";
  ip: string = "";
  /** What an authentication guard put on the request (@nestjs/passport: validate()'s value). */
  user: any = null;
  /** The route's parameters (/:id), by name. */
  params: Record<string, string> = {};
  /** (what middleware, guards and interceptors add: request.terminal = t; an open class, read by name) */
  __expando: Record<string, any> = {};
  private readonly all: Map<string, string> = new Map<string, string>();
  private readonly queries: Map<string, string> = new Map<string, string>();
  private source: HttpRequest | null = null; // the server's request: every header it carries

  /** Sets a header as it arrived (the framework calls this while building the request). */
  setHeaderValue(name: string, value: string): void {
    this.all.set(name.toLowerCase(), value);
    if (name.toLowerCase() === "authorization") {
      this.headers.authorization = value;
    }
  }

  /** The server's request the other headers are read from (the framework calls this while building the request). */
  setSource(source: HttpRequest): void {
    this.source = source;
    for (const name of source.headerNames()) {
      const v = source.header(name);
      if (name === "authorization") {
        this.headers.authorization = v;
      } else {
        this.headers.__expando[name] = v;
      }
    }
  }

  /** Sets a query string value (the framework calls this while building the request). */
  setQueryValue(name: string, value: string): void {
    this.queries.set(name, value);
  }

  /**
   * A request header, matched without case, or null when it is absent.
   * @example req.header("content-type")
   */
  header(name: string): string | null {
    const v = this.all.get(name.toLowerCase());
    if (v !== null) {
      return v;
    }
    const src = this.source;
    if (src !== null) {
      const raw = src.header(name);
      return raw.length > 0 ? raw : null;
    }
    return null;
  }

  /** Same as `header(name)`. */
  get(name: string): string | null {
    return this.header(name);
  }

  /** A query string value, or null when it is absent. */
  query(name: string): string | null {
    return this.queries.get(name);
  }
}

/**
 * The response a handler writes, as Express's: status, headers, then a body.
 * Compatibility: the body is text; `send` and `json` end the response, as in Express.
 * @example res.status(200).setHeader("Content-Type", "text/csv").send(csv)
 */
export class Response {
  statusCode: int = 200;
  /** True once `send`, `json` or `end` was called. */
  finished: boolean = false;
  private readonly names: string[] = [];
  private readonly values: string[] = [];
  private text: string = "";
  private readonly hooks: ((res: Response) => void)[] = [];
  private hooksRan: boolean = false;

  /** Sets the status code. */
  status(code: int): Response {
    this.statusCode = code;
    return this;
  }

  /** Sets a header, replacing one of the same name (matched without case). */
  setHeader(name: string, value: string): Response {
    const lower = name.toLowerCase();
    for (let i = 0; i < this.names.length; i++) {
      if (this.names[i].toLowerCase() === lower) {
        this.values[i] = value;
        return this;
      }
    }
    this.names.push(name);
    this.values.push(value);
    return this;
  }

  /** Same as `setHeader`. */
  header(name: string, value: string): Response {
    return this.setHeader(name, value);
  }

  /** Same as `setHeader`. */
  set(name: string, value: string): Response {
    return this.setHeader(name, value);
  }

  /** A header set so far, or null. */
  getHeader(name: string): string | null {
    const lower = name.toLowerCase();
    for (let i = 0; i < this.names.length; i++) {
      if (this.names[i].toLowerCase() === lower) {
        return this.values[i];
      }
    }
    return null;
  }

  /** Sets Content-Type: "json" and "csv" become their media types, anything with a slash is kept. */
  type(kind: string): Response {
    if (kind === "json") {
      return this.setHeader("Content-Type", "application/json; charset=utf-8");
    }
    if (kind === "csv") {
      return this.setHeader("Content-Type", "text/csv; charset=utf-8");
    }
    if (kind === "html") {
      return this.setHeader("Content-Type", "text/html; charset=utf-8");
    }
    return this.setHeader("Content-Type", kind);
  }

  /** Sets Content-Disposition so the client saves the body as a file with this name. */
  attachment(filename: string): Response {
    return this.setHeader("Content-Disposition", `attachment; filename="${filename}"`);
  }

  /**
   * Sends a body and ends the response. A string: Content-Type defaults to text/html, as in Express. A Buffer:
   * its bytes as they are, Content-Type application/octet-stream unless one is set (res.send(xlsx)).
   */
  send(body: string | Buffer): Response {
    if (typeof body === "string") {
      if (this.getHeader("Content-Type") === null) {
        this.setHeader("Content-Type", "text/html; charset=utf-8");
      }
      this.text = body;
      this.finished = true;
      return this;
    }
    if (this.getHeader("Content-Type") === null) {
      this.setHeader("Content-Type", "application/octet-stream");
    }
    this.text = body.toString("xisto:raw");
    this.finished = true;
    return this;
  }

  /** Sends a value as JSON and ends the response: res.json({ ok: true }) or res.json(dto). */
  json(body: object): Response {
    if (this.getHeader("Content-Type") === null) {
      this.setHeader("Content-Type", "application/json; charset=utf-8");
    }
    this.text = JSON.stringify(body);
    this.finished = true;
    return this;
  }

  /** Appends text to the body (written when the response ends). */
  write(chunk: string): boolean {
    this.text = this.text + chunk;
    return true;
  }

  /** Ends the response, with an optional last piece of text. */
  /** once("drain", fn): the body is buffered, so it never waits: fn runs now. Other events never come. */
  once(event: string, fn: () => void): Response {
    if (event === "drain") fn();
    return this;
  }

  end(chunk: string = ""): Response {
    this.text = this.text + chunk;
    this.finished = true;
    return this;
  }

  /**
   * destroy(err): Node closes the socket in the middle of the body. The body here is buffered, so nothing of it has
   * gone out yet: it is dropped and the request ends with a 500 and no body (the client sees a failure either way).
   */
  destroy(err: Error | null = null): Response {
    this.text = "";
    this.statusCode = 500;
    this.finished = true;
    return this;
  }

  /** Runs `fn` just before the headers go out, when the status and Content-Type are final (on-headers). */
  onHeaders(fn: (res: Response) => void): void {
    this.hooks.push(fn);
  }

  /**
   * Called by the framework once, as the response is sent: records the Content-Type it will send ("" for none)
   * and runs the onHeaders callbacks.
   */
  headersReady(contentType: string): void {
    if (this.hooksRan) {
      return;
    }
    this.hooksRan = true;
    if (contentType.length > 0 && this.getHeader("Content-Type") === null) {
      this.setHeader("Content-Type", contentType);
    }
    for (const fn of this.hooks) {
      fn(this);
    }
  }

  /** The status, as Express names the getter too. */
  getStatus(): int {
    return this.statusCode;
  }

  /** The body written so far. */
  getBody(): string {
    return this.text;
  }

  /** The number of headers set; `headerName(i)` and `headerValue(i)` read them in order. */
  headerCount(): int {
    return this.names.length;
  }
  headerName(i: int): string {
    return this.names[i];
  }
  headerValue(i: int): string {
    return this.values[i];
  }
}

/** The callback a middleware calls to hand the request on. */
export type NextFunction = () => void;

/** A middleware: helmet(), compression() and app.use(fn) take this shape. */
export type RequestHandler = (req: Request, res: Response, next: NextFunction) => void;
