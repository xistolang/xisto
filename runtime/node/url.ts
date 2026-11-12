// xisto:url: the WHATWG URL and URLSearchParams, and AbortController / AbortSignal (encodeURIComponent & co are
// built in): globals
// in Node (and in every xisto module: the compiler loads this one when a program names them), also importable from
// "node:url". Parsing follows the WHATWG URL Standard for the special schemes (http, https, ws, wss, ftp, file):
// lower-case scheme and host, default ports dropped, dot segments removed, percent-encoding per component; other
// schemes keep an opaque path. Not here: IDNA (non-ASCII hosts), IPv4 shorthand (127.1), blob: origins.


function utf8(s: string): Uint8Array {
  return new TextEncoder().encode(s);
}

function fromUtf8(bytes: Uint8Array): string {
  return new TextDecoder().decode(bytes);
}

function hexValue(code: number): int {
  const c = Math.trunc(code);
  if (c >= 48 && c <= 57) return c - 48;
  if (c >= 65 && c <= 70) return c - 55;
  if (c >= 97 && c <= 102) return c - 87;
  return -1;
}

// (no module-level constants: this module is loaded after the program's, so its top-level code would run last)
function pct(b: int): string {
  const hex = "0123456789ABCDEF";
  return "%" + hex.charAt(b >> 4) + hex.charAt(b & 15);
}

// ---- application/x-www-form-urlencoded ----

function formEncode(s: string): string {
  const bytes = utf8(s);
  let out = "";
  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];
    if ((b >= 48 && b <= 57) || (b >= 65 && b <= 90) || (b >= 97 && b <= 122) || b === 42 || b === 45 || b === 46 || b === 95) {
      out += String.fromCharCode(b);
    } else if (b === 32) {
      out += "+";
    } else {
      out += pct(b);
    }
  }
  return out;
}

// percent-decoding as the URL Standard does it: a % without two hex digits stays as it is
function percentDecode(s: string): string {
  if (s.indexOf("%") < 0) return s;
  const src = utf8(s);
  const out = new Uint8Array(src.length);
  let n = 0;
  for (let i = 0; i < src.length; i++) {
    const b = src[i];
    if (b === 37 && i + 2 < src.length) {
      const h1 = hexValue(src[i + 1]);
      const h2 = hexValue(src[i + 2]);
      if (h1 >= 0 && h2 >= 0) {
        out[n] = h1 * 16 + h2;
        n++;
        i += 2;
        continue;
      }
    }
    out[n] = b;
    n++;
  }
  return fromUtf8(out.subarray(0, n));
}

function formDecode(s: string): string {
  return percentDecode(s.split("+").join(" "));
}

class SearchPair {
  name: string;
  value: string;
  constructor(name: string, value: string) {
    this.name = name;
    this.value = value;
  }
}

/**
 * A query string's name/value pairs, in order (WHATWG URLSearchParams).
 * @example new URLSearchParams({ grant_type: "client_credentials", scope }).toString()
 */
export class URLSearchParams {
  private list: SearchPair[] = [];
  private owner: URL | null = null;

  /** From a query string ("a=1&b=2", with or without "?") or from an object's keys and values. */
  constructor(init: string | Record<string, string> = "") {
    if (typeof init === "string") {
      this.parse(init as string);
    } else {
      const rec = init as Record<string, string>;
      for (const k of Object.keys(rec)) {
        const v = rec[k];
        this.list.push(new SearchPair(k, v ?? ""));
      }
    }
  }

  private parse(text: string): void {
    this.list = [];
    let q = text;
    if (q.startsWith("?")) q = q.slice(1);
    for (const part of q.split("&")) {
      if (part.length === 0) continue;
      const eq = part.indexOf("=");
      const name = eq >= 0 ? part.slice(0, eq) : part;
      const value = eq >= 0 ? part.slice(eq + 1) : "";
      this.list.push(new SearchPair(formDecode(name), formDecode(value)));
    }
  }

  // (URL: its searchParams follow its query, and write it back)
  __attach(owner: URL, query: string): void {
    this.owner = owner;
    this.parse(query);
  }
  __reset(query: string): void {
    this.parse(query);
  }

  private update(): void {
    const o = this.owner;
    if (o !== null) {
      o.__setQueryFromParams(this.toString());
    }
  }

  /** The number of pairs. */
  get size(): int {
    return this.list.length;
  }
  /** Adds a pair at the end. */
  append(name: string, value: string): void {
    this.list.push(new SearchPair(name, value));
    this.update();
  }
  /** Removes every pair with that name (and that value, when given). */
  delete(name: string, value: string | null = null): void {
    this.list = this.list.filter((p: SearchPair): boolean => !(p.name === name && (value === null || p.value === value)));
    this.update();
  }
  /** The first value of that name, or null. */
  get(name: string): string | null {
    for (const p of this.list) {
      if (p.name === name) return p.value;
    }
    return null;
  }
  /** Every value of that name, in order. */
  getAll(name: string): string[] {
    const out: string[] = [];
    for (const p of this.list) {
      if (p.name === name) out.push(p.value);
    }
    return out;
  }
  /** True when a pair has that name (and that value, when given). */
  has(name: string, value: string | null = null): boolean {
    for (const p of this.list) {
      if (p.name === name && (value === null || p.value === value)) return true;
    }
    return false;
  }
  /** Sets the first pair of that name to value and removes the others (or adds the pair). */
  set(name: string, value: string): void {
    let found = false;
    const out: SearchPair[] = [];
    for (const p of this.list) {
      if (p.name !== name) {
        out.push(p);
      } else if (!found) {
        found = true;
        out.push(new SearchPair(name, value));
      }
    }
    if (!found) out.push(new SearchPair(name, value));
    this.list = out;
    this.update();
  }
  /** Sorts the pairs by name (stable, by UTF-16 code units). */
  sort(): void {
    const idx: int[] = [];
    for (let i = 0; i < this.list.length; i++) idx.push(i);
    const l = this.list;
    idx.sort((a: int, b: int): number => {
      const x = l[a].name;
      const y = l[b].name;
      if (x < y) return -1;
      if (x > y) return 1;
      return a - b;
    });
    const out: SearchPair[] = [];
    for (const i of idx) out.push(l[i]);
    this.list = out;
    this.update();
  }
  /** Calls fn(value, name) for each pair. */
  forEach(fn: (value: string, name: string) => void): void {
    for (const p of this.list) fn(p.value, p.name);
  }
  /** The names, in order. */
  keys(): string[] {
    return this.list.map((p: SearchPair): string => p.name);
  }
  /** The values, in order. */
  values(): string[] {
    return this.list.map((p: SearchPair): string => p.value);
  }
  /** The [name, value] pairs, in order. */
  entries(): string[][] {
    return this.list.map((p: SearchPair): string[] => [p.name, p.value]);
  }
  *[Symbol.iterator](): Generator<string[]> {
    for (const p of this.list) yield [p.name, p.value];
  }
  /** The pairs as application/x-www-form-urlencoded text: a=1&b=x+y (no "?"). */
  toString(): string {
    const parts: string[] = [];
    for (const p of this.list) parts.push(formEncode(p.name) + "=" + formEncode(p.value));
    return parts.join("&");
  }
}

// ---- URL ----

function defaultPort(scheme: string): string {
  if (scheme === "http" || scheme === "ws") return "80";
  if (scheme === "https" || scheme === "wss") return "443";
  if (scheme === "ftp") return "21";
  return "";
}

function isSpecial(scheme: string): boolean {
  return scheme === "http" || scheme === "https" || scheme === "ws" || scheme === "wss" || scheme === "ftp" || scheme === "file";
}


// percent-encodes the code points of a component that its set lists (and C0 controls, and every non-ASCII one)
function encodeSet(s: string, set: string): string {
  let plain = true;
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c < 0x21 || c > 0x7e || set.indexOf(s.charAt(i)) >= 0) {
      plain = false;
      break;
    }
  }
  if (plain) return s;
  const bytes = utf8(s);
  let out = "";
  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];
    if (b < 0x20 || b > 0x7e || set.indexOf(String.fromCharCode(b)) >= 0) {
      out += pct(b);
    } else {
      out += String.fromCharCode(b);
    }
  }
  return out;
}

function invalid(input: string): TypeError {
  return new TypeError("Invalid URL");
}

// the path's segments with "." and ".." resolved (WHATWG: a path that ends in one of them ends in "/")
function normalizePath(path: string, special: boolean): string {
  const raw = special ? path.split("\\").join("/") : path;
  const segs = raw.split("/");
  const out: string[] = [];
  for (let i = 1; i < segs.length; i++) {
    const s = segs[i];
    const low = s.toLowerCase();
    const isDot = s === "." || low === "%2e";
    const isDotDot = s === ".." || low === ".%2e" || low === "%2e." || low === "%2e%2e";
    const last = i === segs.length - 1;
    if (isDotDot) {
      if (out.length > 0) out.pop();
      if (last) out.push("");
    } else if (isDot) {
      if (last) out.push("");
    } else {
      out.push(s);
    }
  }
  return "/" + out.join("/");
}

/**
 * A parsed URL (WHATWG): new URL("https://a.com/x?y=1#z"), or relative to a base: new URL("../c.css", base).
 * @example new URL("https://Host.com:443/a/./b/../c?q=1").href // "https://host.com/a/c?q=1"
 */
export class URL {
  private scheme: string = "";
  private user: string = "";
  private pass: string = "";
  private host_: string = "";
  private port_: string = "";
  private hasAuthority: boolean = false;
  private path: string = "";
  private query: string | null = null;
  private fragment: string | null = null;
  private params: URLSearchParams = new URLSearchParams();

  constructor(input: string, base: string | null = null) {
    const text = input.trim().split("\t").join("").split("\n").join("").split("\r").join("");
    const m = /^([a-zA-Z][a-zA-Z0-9+.\-]*):/.exec(text);
    if (m !== null) {
      this.parseAbsolute(m[1].toLowerCase(), text.slice(m[0].length), input);
    } else {
      if (base === null) throw invalid(input);
      const b = new URL(base);
      this.resolve(b, text, input);
    }
    this.params.__attach(this, this.query ?? "");
  }

  private parseAbsolute(scheme: string, rest0: string, input: string): void {
    this.scheme = scheme;
    let rest = rest0;
    if (scheme === "file") {
      // file://host/path: exactly two slashes open the host (file:///x has none); file:/x is a path
      const two = rest.length >= 2 && "/\\".indexOf(rest.charAt(0)) >= 0 && "/\\".indexOf(rest.charAt(1)) >= 0;
      if (two) {
        this.parseAuthorityAndRest(rest.slice(2), input);
      } else {
        this.hasAuthority = true;
        this.splitRest(rest, true, false);
      }
      return;
    }
    if (isSpecial(scheme)) {
      let i = 0;
      while (i < rest.length && (rest.charAt(i) === "/" || rest.charAt(i) === "\\")) i++;
      this.parseAuthorityAndRest(rest.slice(i), input);
      return;
    }
    if (rest.startsWith("//")) {
      this.parseAuthorityAndRest(rest.slice(2), input);
      return;
    }
    this.splitRest(rest, false, true);
  }

  private parseAuthorityAndRest(s: string, input: string): void {
    const special = isSpecial(this.scheme);
    let end = s.length;
    for (let i = 0; i < s.length; i++) {
      const c = s.charAt(i);
      if (c === "/" || c === "?" || c === "#" || (special && c === "\\")) {
        end = i;
        break;
      }
    }
    let auth = s.slice(0, end);
    const at = auth.lastIndexOf("@");
    if (at >= 0) {
      const info = auth.slice(0, at);
      auth = auth.slice(at + 1);
      const colon = info.indexOf(":");
      this.user = encodeSet(colon >= 0 ? info.slice(0, colon) : info, " \"#<>?`{}/:;=@[\\]^|");
      this.pass = colon >= 0 ? encodeSet(info.slice(colon + 1), " \"#<>?`{}/:;=@[\\]^|") : "";
    }
    let host = auth;
    let port = "";
    const close = auth.lastIndexOf("]");
    const colon = auth.lastIndexOf(":");
    if (colon >= 0 && colon > close) {
      host = auth.slice(0, colon);
      port = auth.slice(colon + 1);
    }
    if (port.length > 0) {
      if (!/^[0-9]+$/.test(port)) throw invalid(input);
      const n = parseInt(port, 10);
      if (n > 65535) throw invalid(input);
      port = String(n);
      if (port === defaultPort(this.scheme)) port = "";
    }
    host = percentDecode(host).toLowerCase();
    if (special && host.length === 0 && this.scheme !== "file") throw invalid(input);
    if (/[\u0000\t\n\r #/<>?@[\\\]^|%]/.test(host.startsWith("[") ? "" : host)) throw invalid(input);
    this.host_ = host;
    this.port_ = port;
    this.hasAuthority = true;
    this.splitRest(s.slice(end), special, false);
  }

  // the path, query and fragment after the authority (or after "scheme:" for an opaque path)
  private splitRest(rest: string, special: boolean, opaque: boolean): void {
    let s = rest;
    const hash = s.indexOf("#");
    if (hash >= 0) {
      this.fragment = encodeSet(s.slice(hash + 1), " \"<>`");
      s = s.slice(0, hash);
    }
    const q = s.indexOf("?");
    if (q >= 0) {
      this.query = encodeSet(s.slice(q + 1), special ? " \"#<>'" : " \"#<>");
      s = s.slice(0, q);
    }
    if (opaque) {
      this.path = encodeSet(s, "");
      return;
    }
    if (special || s.length > 0) {
      this.path = encodeSet(normalizePath(s.length > 0 && (s.startsWith("/") || (special && s.startsWith("\\"))) ? s : "/" + s, special), " \"#<>?`{}");
    } else {
      this.path = "";
    }
  }

  private resolve(b: URL, ref: string, input: string): void {
    const special = isSpecial(b.scheme);
    if (!b.hasAuthority && !special) {
      if (ref.startsWith("#")) {
        this.copyBase(b);
        this.query = b.query;
        this.fragment = encodeSet(ref.slice(1), " \"<>`");
        return;
      }
      throw invalid(input);
    }
    this.scheme = b.scheme;
    if (ref.startsWith("//") || (special && (ref.startsWith("\\\\") || ref.startsWith("/\\") || ref.startsWith("\\/")))) {
      this.parseAuthorityAndRest(ref.slice(2), input);
      return;
    }
    this.copyBase(b);
    if (ref.length === 0) {
      this.path = b.path;
      this.query = b.query;
      return;
    }
    if (ref.startsWith("/") || (special && ref.startsWith("\\"))) {
      this.splitRest(ref, special, false);
      return;
    }
    if (ref.startsWith("?")) {
      this.path = b.path;
      this.splitRest(ref, special, false);
      this.path = b.path;
      return;
    }
    if (ref.startsWith("#")) {
      this.path = b.path;
      this.query = b.query;
      this.fragment = encodeSet(ref.slice(1), " \"<>`");
      return;
    }
    const dir = b.path.slice(0, b.path.lastIndexOf("/") + 1);
    this.splitRest((dir.length > 0 ? dir : "/") + ref, special, false);
  }

  private copyBase(b: URL): void {
    this.scheme = b.scheme;
    this.user = b.user;
    this.pass = b.pass;
    this.host_ = b.host_;
    this.port_ = b.port_;
    this.hasAuthority = b.hasAuthority;
  }

  // (URLSearchParams: a change to url.searchParams writes the query)
  __setQueryFromParams(text: string): void {
    this.query = text.length > 0 ? text : null;
  }

  /** The whole URL as text. */
  get href(): string {
    let out = this.scheme + ":";
    if (this.hasAuthority) {
      out += "//";
      if (this.user.length > 0 || this.pass.length > 0) {
        out += this.user + (this.pass.length > 0 ? ":" + this.pass : "") + "@";
      }
      out += this.host;
    }
    out += this.path;
    if (this.query !== null) out += "?" + this.query;
    if (this.fragment !== null) out += "#" + this.fragment;
    return out;
  }
  /** "https:" */
  get protocol(): string {
    return this.scheme + ":";
  }
  get username(): string {
    return this.user;
  }
  get password(): string {
    return this.pass;
  }
  /** The host name and the port, when it is not the scheme's default: "example.com:8080". */
  get host(): string {
    return this.port_.length > 0 ? this.host_ + ":" + this.port_ : this.host_;
  }
  get hostname(): string {
    return this.host_;
  }
  /** The port as text ("" for the scheme's default). */
  get port(): string {
    return this.port_;
  }
  /** "/a/b" */
  get pathname(): string {
    return this.path;
  }
  set pathname(p: string) {
    if (!this.hasAuthority && !isSpecial(this.scheme)) return;
    this.path = encodeSet(normalizePath(p.startsWith("/") ? p : "/" + p, isSpecial(this.scheme)), " \"#<>?`{}");
  }
  /** "?a=1", or "" when there is no query. */
  get search(): string {
    const q = this.query;
    return q === null || q.length === 0 ? "" : "?" + q;
  }
  set search(s: string) {
    const t = s.startsWith("?") ? s.slice(1) : s;
    this.query = s.length === 0 ? null : encodeSet(t, isSpecial(this.scheme) ? " \"#<>'" : " \"#<>");
    this.params.__reset(this.query ?? "");
  }
  /** "#part", or "" when there is no fragment. */
  get hash(): string {
    const f = this.fragment;
    return f === null || f.length === 0 ? "" : "#" + f;
  }
  set hash(h: string) {
    const t = h.startsWith("#") ? h.slice(1) : h;
    this.fragment = h.length === 0 ? null : encodeSet(t, " \"<>`");
  }
  /** "https://example.com:8080", or "null" for schemes without an origin (file:, data:...). */
  get origin(): string {
    if (isSpecial(this.scheme) && this.scheme !== "file") return this.scheme + "://" + this.host;
    return "null";
  }
  /** The query's pairs, kept in step with the URL. */
  get searchParams(): URLSearchParams {
    return this.params;
  }
  toString(): string {
    return this.href;
  }
  toJSON(): string {
    return this.href;
  }
  /** True when new URL(input, base) would not throw. */
  static canParse(input: string, base: string | null = null): boolean {
    try {
      new URL(input, base);
      return true;
    } catch (e) {
      return false;
    }
  }
}

// ---- AbortController / AbortSignal ----

/**
 * Tells an operation to stop: fetch(url, { signal }) rejects with an AbortError once abort() is called.
 * @example const c = new AbortController(); setTimeout(() => c.abort(), 5000); await fetch(url, { signal: c.signal });
 */
export class AbortSignal {
  /** True once aborted. */
  aborted: boolean = false;
  /** Why: an Error named AbortError (or TimeoutError), or the value given to abort(reason). */
  reason: unknown = null;
  // (fetch reads byte 0 while it waits: 1 aborted, 2 timed out)
  __flag: Uint8Array = new Uint8Array(1);
  private listeners: (() => void)[] = [];

  /** Throws the reason when aborted. */
  throwIfAborted(): void {
    if (this.aborted) {
      throw this.reason as Error;
    }
  }
  /** Calls fn once, when aborted ("abort" is the only event). */
  addEventListener(type: string, fn: () => void): void {
    if (type === "abort") this.listeners.push(fn);
  }
  // (AbortController.abort and the timeout)
  __abort(reason: unknown, kind: int): void {
    if (this.aborted) return;
    this.aborted = true;
    this.reason = reason;
    this.__flag[0] = kind;
    for (const fn of this.listeners) fn();
    this.listeners = [];
  }
  /** A signal that aborts after ms milliseconds with a TimeoutError. */
  static timeout(ms: number): AbortSignal {
    const s = new AbortSignal();
    setTimeout((): void => {
      s.__abort(new DOMException("The operation was aborted due to timeout", "TimeoutError"), 2);
    }, ms);
    return s;
  }
  /** A signal already aborted. */
  static abort(reason: unknown = null): AbortSignal {
    const s = new AbortSignal();
    s.__abort(reason ?? abortError(), 1);
    return s;
  }
}

function abortError(): Error {
  return new DOMException("This operation was aborted", "AbortError");
}

/** The web's error class: what an aborted fetch rejects with (name AbortError or TimeoutError). */
export class DOMException extends Error {
  constructor(message: string = "", name: string = "Error") {
    super(message);
    this.name = name;
  }
}

/** Owns a signal and aborts it. */
export class AbortController {
  readonly signal: AbortSignal = new AbortSignal();
  /** Aborts the signal (once): its reason is an AbortError, or the reason given. */
  abort(reason: unknown = null): void {
    this.signal.__abort(reason ?? abortError(), 1);
  }
}

// ---- fetch with its options in a value: fetch(url, init), fetch(url, { ...init, headers }) ----

/** fetch's options (the Fetch API's RequestInit): method, headers, body, signal; redirect is accepted (followed). */
export interface RequestInit {
  method?: string;
  headers?: Record<string, string>;
  body?: string;
  signal?: AbortSignal;
  redirect?: string;
}

/** The Fetch API's HeadersInit as xisto takes it: a Record of names and values. */
export type HeadersInit = Record<string, string>;

/** (the compiler calls this for fetch(url, init) when the options are not written in place) */
export function __xs_fetch(url: string, init: RequestInit): Response {
  const method = init.method ?? "GET";
  const headers: Record<string, string> = init.headers ?? {};
  const body = init.body ?? null;
  const signal = init.signal ?? null;
  if (signal !== null) {
    if (body !== null) return fetch(url, { method: method, headers: headers, body: body, signal: signal });
    return fetch(url, { method: method, headers: headers, signal: signal });
  }
  if (body !== null) return fetch(url, { method: method, headers: headers, body: body });
  return fetch(url, { method: method, headers: headers });
}
