// xisto:url: the WHATWG URL and URLSearchParams, and AbortController / AbortSignal (encodeURIComponent & co are
// built in): globals
// in Node (and in every xisto module: the compiler loads this one when a program names them), also importable from
// "node:url". Parsing follows the WHATWG URL Standard for the special schemes (http, https, ws, wss, ftp, file):
// lower-case scheme and host, default ports dropped, dot segments removed, percent-encoding per component; other
// schemes keep an opaque path. Not here: IDNA (non-ASCII hosts), IPv4 shorthand (127.1), blob: origins.
// fileURLToPath and pathToFileURL: node:url's, for POSIX paths.
import { TypeError } from "xisto:errors";
import { resolve as __resolvePath } from "xisto:path";


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

  /** url.format(url, options): the href without the fragment, the query or the credentials when they are false. */
  __format(fragment: boolean, search: boolean, auth: boolean): string {
    let out = this.scheme + ":";
    if (this.hasAuthority) {
      out += "//";
      if (auth && (this.user.length > 0 || this.pass.length > 0)) {
        out += this.user + (this.pass.length > 0 ? ":" + this.pass : "") + "@";
      }
      out += this.host;
    }
    out += this.path;
    if (search && this.query !== null) out += "?" + this.query;
    if (fragment && this.fragment !== null) out += "#" + this.fragment;
    return out;
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
  body?: string | Uint8Array;
  signal?: AbortSignal;
  redirect?: string;
}

declare function fetch_body_bytes(b: Uint8Array): string;
function __xs_bytes_text(b: Uint8Array): string {
  return fetch_body_bytes(b);
}

/** Node's BufferEncoding: the name of an encoding ("utf8", "latin1", "base64", "hex"...). */
export type BufferEncoding = string;

/** The Fetch API's HeadersInit as xisto takes it: a Record of names and values. */
export type HeadersInit = Record<string, string>;

/** (the compiler calls this for fetch(url, init) when the options are not written in place) */
export function __xs_fetch(url: string, init: RequestInit): Response {
  const method = init.method ?? "GET";
  const headers: Record<string, string> = init.headers ?? {};
  const given = init.body ?? null;
  // (bytes are sent as they are: their text here is only how the C side carries them)
  const body: string | null = given === null ? null : typeof given === "string" ? (given as string) : __xs_bytes_text(given as Uint8Array);
  const signal = init.signal ?? null;
  if (signal !== null) {
    if (body !== null) return fetch(url, { method: method, headers: headers, body: body, signal: signal });
    return fetch(url, { method: method, headers: headers, signal: signal });
  }
  if (body !== null) return fetch(url, { method: method, headers: headers, body: body });
  return fetch(url, { method: method, headers: headers });
}

// ---- fileURLToPath / pathToFileURL ----

declare function url_platform(): string;

class UrlCodeError extends TypeError {
  code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}

/** The path a file: URL names, decoded ("file:///a%20b" is "/a b"). */
export function fileURLToPath(url: string | URL): string {
  const u = typeof url === "string" ? new URL(url) : url;
  if (u.protocol !== "file:") throw new UrlCodeError("ERR_INVALID_URL_SCHEME", "The URL must be of scheme file");
  if (u.hostname !== "" && u.hostname !== "localhost") {
    throw new UrlCodeError("ERR_INVALID_FILE_URL_HOST", `File URL host must be "localhost" or empty on ${url_platform()}`);
  }
  const p = u.pathname;
  for (let i = 0; i + 2 < p.length; i++) {
    if (p.charAt(i) === "%" && p.charAt(i + 1) === "2" && (p.charAt(i + 2) === "f" || p.charAt(i + 2) === "F")) {
      throw new UrlCodeError("ERR_INVALID_FILE_URL_PATH", "File URL path must not include encoded / characters");
    }
  }
  return decodeURIComponent(p);
}

/** The file: URL of a path (made absolute), its bytes percent-encoded as Node encodes them (spaces, %, #, ?...). */
export function pathToFileURL(path: string): URL {
  let abs = __resolvePath(path);
  if (path.endsWith("/") && !abs.endsWith("/")) abs += "/";
  const bytes = utf8(abs);
  let out = "file://";
  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];
    const keep = (b >= 48 && b <= 57) || (b >= 65 && b <= 90) || (b >= 97 && b <= 122) || b === 45 || b === 46 || b === 95 || b === 33 ||
      b === 36 || b === 38 || b === 39 || b === 40 || b === 41 || b === 42 || b === 43 || b === 44 || b === 59 || b === 61 || b === 58 ||
      b === 64 || b === 47;
    out += keep ? String.fromCharCode(b) : pct(b);
  }
  return new URL(out);
}

// ---- the legacy API: url.parse, url.format, url.resolve (Node's lib/url.js, the same algorithm) ----
// Differences from Node: non-ASCII host names are kept as written (Node converts them to punycode); with
// parseQueryString the query is a Record<string, string | string[]> and prints as a plain object (Node:
// [Object: null prototype]); a query object given to format() is a Record of strings, numbers, booleans and string
// arrays. (The query strings are read and written here as node:querystring does by default, without loading it:
// its any-typed functions would make every program that names URL carry the field tables of `any`.)

/** What url.format() takes: the parts of a URL, each one optional. */
export interface UrlObject {
  protocol?: string | null;
  slashes?: boolean | null;
  auth?: string | null;
  host?: string | null;
  hostname?: string | null;
  port?: string | number | null;
  pathname?: string | null;
  search?: string | null;
  query?: LegacyQueryInput | string | null;
  hash?: string | null;
  href?: string | null;
  path?: string | null;
}

/** url.format(URL, options)'s options: which parts to keep (all by default; unicode is accepted and ignored). */
export interface URLFormatOptions {
  auth?: boolean | null;
  fragment?: boolean | null;
  search?: boolean | null;
  unicode?: boolean | null;
}

/** A query given to format() as an object: each value a string, number, boolean or string[]. */
export type LegacyQueryInput = Record<string, string | number | boolean | string[]>;

// (http, https, ftp, gopher, file, ws, wss: always written with //)
function slashedProtocol(p0: string | null): boolean {
  if (p0 === null) return false;
  const p = p0.endsWith(":") ? p0.slice(0, p0.length - 1) : p0;
  return p === "http" || p === "https" || p === "ftp" || p === "gopher" || p === "file" || p === "ws" || p === "wss";
}

// node:querystring's parse with its defaults: & and =, at most 1000 pairs, + as a space, %XX as UTF-8 (a malformed
// sequence kept as written), a key given more than once collected into an array
function legacyQsParse(qs: string): Record<string, string | string[]> {
  const out: Record<string, string | string[]> = {};
  if (qs.length === 0) return out;
  const order: string[] = [];
  const values = new Map<string, string[]>();
  let pairs = 0;
  for (const seg of qs.split("&")) {
    if (seg.length === 0) continue;
    if (pairs === 1000) break;
    pairs++;
    const eq = seg.indexOf("=");
    const k = formDecode(eq < 0 ? seg : seg.slice(0, eq));
    const v = eq < 0 ? "" : formDecode(seg.slice(eq + 1));
    const cur = values.get(k);
    if (cur === undefined || cur === null) {
      order.push(k);
      values.set(k, [v]);
    } else {
      cur.push(v);
    }
  }
  for (const k of order) {
    const vs = values.get(k) as string[];
    if (vs.length === 1) out[k] = vs[0];
    else out[k] = vs;
  }
  return out;
}
function qsValue(v: string | number | boolean): string {
  if (typeof v === "string") return encodeURIComponent(v as string);
  if (typeof v === "number") {
    const n = v as number;
    return Number.isFinite(n) ? encodeURIComponent(String(n)) : "";
  }
  return (v as boolean) ? "true" : "false";
}
// node:querystring's stringify with its defaults (a non-finite number is an empty value, an empty array no pair)
function legacyQsStringify(obj: LegacyQueryInput): string {
  let fields = "";
  for (const k of Object.keys(obj)) {
    const v = obj[k];
    const ks = encodeURIComponent(k) + "=";
    if (Array.isArray(v)) {
      const arr = v as string[];
      if (arr.length === 0) continue;
      for (const x of arr) {
        if (fields.length > 0) fields += "&";
        fields += ks + encodeURIComponent(x);
      }
    } else {
      if (fields.length > 0) fields += "&";
      fields += ks + qsValue(v as string | number | boolean);
    }
  }
  return fields;
}
function legacyQsStringifyParsed(obj: Record<string, string | string[]>): string {
  let fields = "";
  for (const k of Object.keys(obj)) {
    const v = obj[k];
    const ks = encodeURIComponent(k) + "=";
    if (typeof v === "string") {
      if (fields.length > 0) fields += "&";
      fields += ks + encodeURIComponent(v as string);
    } else {
      for (const x of v as string[]) {
        if (fields.length > 0) fields += "&";
        fields += ks + encodeURIComponent(x);
      }
    }
  }
  return fields;
}
// (javascript: never has a host, and its "unsafe" characters are not escaped)
function hostlessProtocol(p: string | null): boolean {
  return p === "javascript" || p === "javascript:";
}
// (two nullable strings compared without boxing them)
function sameStr(a: string | null, b: string | null): boolean {
  if (a === null || b === null) return a === null && b === null;
  return (a as string) === (b as string);
}
function nonEmpty(s: string | null): boolean {
  return s !== null && s.length > 0;
}
function jsSpace(c: number): boolean {
  return c === 9 || c === 10 || c === 11 || c === 12 || c === 13 || c === 32 || c === 0xa0 || c === 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
    c === 0x2028 || c === 0x2029 || c === 0x202f || c === 0x205f || c === 0x3000 || c === 0xfeff;
}
function isIpv6Hostname(h: string): boolean {
  return h.length > 0 && h.charAt(0) === "[" && h.charAt(h.length - 1) === "]";
}
function legacyAutoEscape(c: number): string {
  if (c === 9) return "%09";
  if (c === 10) return "%0A";
  if (c === 13) return "%0D";
  if (c === 32) return "%20";
  if (c === 34) return "%22";
  if (c === 39) return "%27";
  if (c === 60) return "%3C";
  if (c === 62) return "%3E";
  if (c === 92) return "%5C";
  if (c === 94) return "%5E";
  if (c === 96) return "%60";
  if (c === 123) return "%7B";
  if (c === 124) return "%7C";
  if (c === 125) return "%7D";
  return "";
}
// RFC 2396's delimiters and unwise characters (and the single quote) percent-encoded
function autoEscapeStr(rest: string): string {
  let escaped = "";
  let lastEscapedPos = 0;
  for (let i = 0; i < rest.length; ++i) {
    const e = legacyAutoEscape(rest.charCodeAt(i));
    if (e.length > 0) {
      if (i > lastEscapedPos) escaped += rest.slice(lastEscapedPos, i);
      escaped += e;
      lastEscapedPos = i + 1;
    }
  }
  if (lastEscapedPos === 0) return rest;
  if (lastEscapedPos < rest.length) escaped += rest.slice(lastEscapedPos);
  return escaped;
}
// the auth of format(): every character but A-Z a-z 0-9 ! - . _ ~ ' ( ) * : percent-encoded as UTF-8
function encodeAuth(s: string): string {
  let out = "";
  const bytes = utf8(s);
  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];
    const keep = (b >= 48 && b <= 57) || (b >= 65 && b <= 90) || (b >= 97 && b <= 122) || b === 33 || b === 45 || b === 46 || b === 95 ||
      b === 126 || b === 39 || b === 40 || b === 41 || b === 42 || b === 58;
    out += keep ? String.fromCharCode(b) : pct(b);
  }
  return out;
}
// the length of a scheme and its ":" at the start of s (/^[a-z0-9.+-]+:/i), 0 for none
function protocolLength(s: string): int {
  let i = 0;
  while (i < s.length) {
    const c = s.charCodeAt(i);
    const ok = (c >= 97 && c <= 122) || (c >= 65 && c <= 90) || (c >= 48 && c <= 57) || c === 46 || c === 43 || c === 45;
    if (!ok) break;
    i++;
  }
  return i > 0 && i < s.length && s.charAt(i) === ":" ? i + 1 : 0;
}
// /^\/\/[^@/]+@[^@/]+/: "//user@host"
function hostPatternTest(s: string): boolean {
  if (!s.startsWith("//")) return false;
  let i = 2;
  while (i < s.length && s.charAt(i) !== "@" && s.charAt(i) !== "/") i++;
  if (i === 2 || i >= s.length || s.charAt(i) !== "@") return false;
  return i + 1 < s.length && s.charAt(i + 1) !== "@" && s.charAt(i + 1) !== "/";
}
// /^(\/\/?(?!\/)[^?\s]*)(\?[^\s]*)?$/: a path (and query) alone; where its query starts (-1: none), -2 for no match
function simplePathMatch(s: string): int {
  if (!s.startsWith("/")) return -2;
  let i = 1;
  if (s.charAt(1) === "/") i = 2;
  if (s.charAt(i) === "/") return -2;
  while (i < s.length && s.charAt(i) !== "?" && !jsSpace(s.charCodeAt(i))) i++;
  if (i === s.length) return -1;
  if (s.charAt(i) !== "?") return -2;
  for (let j = i + 1; j < s.length; j++) if (jsSpace(s.charCodeAt(j))) return -2;
  return i;
}
function forbiddenHostChar(h: string, ipv6: boolean): boolean {
  for (let i = 0; i < h.length; i++) {
    const c = h.charAt(i);
    if (c === "\0" || c === "\t" || c === "\n" || c === "\r" || c === " " || c === "#" || c === "%" || c === "/" || c === "<" || c === ">" ||
      c === "?" || c === "@" || c === "\\" || c === "^" || c === "|") return true;
    if (!ipv6 && (c === ":" || c === "[" || c === "]")) return true;
  }
  return false;
}

let legacyWarned = false;
// DEP0169, once, as Node prints it for url.parse() called from the program's own code
function warnUrlParse(): void {
  if (legacyWarned) return;
  legacyWarned = true;
  if (process.env["NODE_NO_WARNINGS"] === "1") return;
  const text = `(node:${process.pid}) [DEP0169] DeprecationWarning: \`url.parse()\` behavior is not standardized and prone to errors that have security implications. Use the WHATWG URL API instead. CVEs are not issued for \`url.parse()\` vulnerabilities.\n(Use \`node --trace-deprecation ...\` to show where the warning was created)`;
  Promise.resolve(0).then((_: number) => {
    console.error(text);
  });
}

/** A URL as the legacy url.parse() splits it; a part that is not there is null. */
export class Url {
  protocol: string | null = null;
  slashes: boolean | null = null;
  auth: string | null = null;
  host: string | null = null;
  port: string | null = null;
  hostname: string | null = null;
  hash: string | null = null;
  search: string | null = null;
  query: any = null;
  pathname: string | null = null;
  path: string | null = null;
  href: string | null = null;

  /** Fills this Url from the text (Node's Url.prototype.parse). */
  parse(url: string, parseQueryString: boolean = false, slashesDenoteHost: boolean = false): Url {
    let hasHash = false;
    let hasAt = false;
    let start = -1;
    let end = -1;
    let rest = "";
    let lastPos = 0;
    let inWs = false;
    let split = false;
    for (let i = 0; i < url.length; ++i) {
      const code = url.charCodeAt(i);
      const isWs = code < 33 || code === 0xa0 || code === 0xfeff;
      if (start === -1) {
        if (isWs) continue;
        lastPos = start = i;
      } else if (inWs) {
        if (!isWs) {
          end = -1;
          inWs = false;
        }
      } else if (isWs) {
        end = i;
        inWs = true;
      }
      if (!split) {
        if (code === 64) {
          hasAt = true;
        } else if (code === 35) {
          hasHash = true;
          split = true;
        } else if (code === 63) {
          split = true;
        } else if (code === 92) {
          if (i - lastPos > 0) rest += url.slice(lastPos, i);
          rest += "/";
          lastPos = i + 1;
        }
      } else if (!hasHash && code === 35) {
        hasHash = true;
      }
    }
    if (start !== -1) {
      if (lastPos === start) {
        if (end === -1) rest = start === 0 ? url : url.slice(start);
        else rest = url.slice(start, end);
      } else if (end === -1 && lastPos < url.length) {
        rest += url.slice(lastPos);
      } else if (end !== -1 && lastPos < end) {
        rest += url.slice(lastPos, end);
      }
    }

    if (!slashesDenoteHost && !hasHash && !hasAt) {
      const q = simplePathMatch(rest);
      if (q !== -2) {
        this.path = rest;
        this.href = rest;
        this.pathname = q === -1 ? rest : rest.slice(0, q);
        if (q !== -1) {
          this.search = rest.slice(q);
          if (parseQueryString) this.query = legacyQsParse(rest.slice(q + 1));
          else this.query = rest.slice(q + 1);
        } else if (parseQueryString) {
          this.search = null;
          this.query = legacyQsParse("");
        }
        return this;
      }
    }

    let lowerProto: string | null = null;
    let proto = "";
    const plen = protocolLength(rest);
    if (plen > 0) {
      proto = rest.slice(0, plen);
      lowerProto = proto.toLowerCase();
      this.protocol = lowerProto;
      rest = rest.slice(plen);
    }

    let slashes = false;
    if (slashesDenoteHost || plen > 0 || hostPatternTest(rest)) {
      slashes = rest.startsWith("//");
      if (slashes && !(plen > 0 && hostlessProtocol(lowerProto))) {
        rest = rest.slice(2);
        this.slashes = true;
      }
    }

    if (!hostlessProtocol(lowerProto) && (slashes || (plen > 0 && !slashedProtocol(proto)))) {
      let hostEnd = -1;
      let atSign = -1;
      let nonHost = -1;
      for (let i = 0; i < rest.length; ++i) {
        const c = rest.charCodeAt(i);
        if (c === 9 || c === 10 || c === 13) {
          rest = rest.slice(0, i) + rest.slice(i + 1);
          i -= 1;
        } else if (c === 32 || c === 34 || c === 37 || c === 39 || c === 59 || c === 60 || c === 62 || c === 92 || c === 94 || c === 96 ||
          c === 123 || c === 124 || c === 125) {
          if (nonHost === -1) nonHost = i;
        } else if (c === 35 || c === 47 || c === 63) {
          if (nonHost === -1) nonHost = i;
          hostEnd = i;
        } else if (c === 64) {
          atSign = i;
          nonHost = -1;
        }
        if (hostEnd !== -1) break;
      }
      start = 0;
      if (atSign !== -1) {
        this.auth = decodeURIComponent(rest.slice(0, atSign));
        start = atSign + 1;
      }
      if (nonHost === -1) {
        this.host = rest.slice(start);
        rest = "";
      } else {
        this.host = rest.slice(start, nonHost);
        rest = rest.slice(nonHost);
      }
      this.parseHost();
      if (this.hostname === null) this.hostname = "";
      let hostname = this.hostname ?? "";
      const ipv6Hostname = isIpv6Hostname(hostname);
      if (!ipv6Hostname) {
        for (let i = 0; i < hostname.length; ++i) {
          const c = hostname.charAt(i);
          if (c === "/" || c === "\\" || c === "#" || c === "?" || c === ":") {
            if (c === ":") throw new UrlCodeError("ERR_INVALID_ARG_VALUE", `The argument 'url' ${url}. Received 'Invalid port in url'`);
            this.hostname = hostname.slice(0, i);
            rest = "/" + hostname.slice(i) + rest;
            break;
          }
        }
      }
      hostname = this.hostname ?? "";
      if (hostname.length > 255) this.hostname = "";
      else this.hostname = hostname.toLowerCase();
      const hn = this.hostname ?? "";
      if (hn !== "" && forbiddenHostChar(hn, ipv6Hostname)) throw new UrlCodeError("ERR_INVALID_URL", "Invalid URL");
      const p = nonEmpty(this.port) ? ":" + (this.port ?? "") : "";
      this.host = hn + p;
      if (ipv6Hostname) {
        this.hostname = hn.slice(1, hn.length - 1);
        if (!rest.startsWith("/")) rest = "/" + rest;
      }
    }

    if (!hostlessProtocol(lowerProto)) rest = autoEscapeStr(rest);

    let questionIdx = -1;
    let hashIdx = -1;
    for (let i = 0; i < rest.length; ++i) {
      const c = rest.charAt(i);
      if (c === "#") {
        this.hash = rest.slice(i);
        hashIdx = i;
        break;
      } else if (c === "?" && questionIdx === -1) {
        questionIdx = i;
      }
    }
    if (questionIdx !== -1) {
      let q = "";
      if (hashIdx === -1) {
        this.search = rest.slice(questionIdx);
        q = rest.slice(questionIdx + 1);
      } else {
        this.search = rest.slice(questionIdx, hashIdx);
        q = rest.slice(questionIdx + 1, hashIdx);
      }
      if (parseQueryString) this.query = legacyQsParse(q);
      else this.query = q;
    } else if (parseQueryString) {
      this.search = null;
      this.query = legacyQsParse("");
    }
    const useQuestionIdx = questionIdx !== -1 && (hashIdx === -1 || questionIdx < hashIdx);
    const firstIdx = useQuestionIdx ? questionIdx : hashIdx;
    if (firstIdx === -1) {
      if (rest.length > 0) this.pathname = rest;
    } else if (firstIdx > 0) {
      this.pathname = rest.slice(0, firstIdx);
    }
    if (slashedProtocol(lowerProto) && nonEmpty(this.hostname) && !nonEmpty(this.pathname)) this.pathname = "/";
    if (nonEmpty(this.pathname) || nonEmpty(this.search)) this.path = (this.pathname ?? "") + (this.search ?? "");
    this.href = this.format();
    return this;
  }

  /** The port at the end of host, taken out of it into port; the rest is the hostname. */
  parseHost(): void {
    let host = this.host ?? "";
    let i = host.length;
    while (i > 0 && host.charCodeAt(i - 1) >= 48 && host.charCodeAt(i - 1) <= 57) i--;
    if (i > 0 && host.charAt(i - 1) === ":") {
      const port = host.slice(i - 1);
      if (port !== ":") this.port = port.slice(1);
      host = host.slice(0, host.length - port.length);
    }
    if (host.length > 0) this.hostname = host;
  }

  /** The URL as text, from its parts (Node's Url.prototype.format). */
  format(): string {
    let q = "";
    const query: any = this.query;
    if (query !== null && typeof query === "object") q = legacyQsStringifyParsed(query as Record<string, string | string[]>);
    return legacyFormat(this.protocol, this.slashes, this.auth, this.host, this.hostname, this.port, this.pathname, this.search, q, this.hash);
  }

  /** The URL that relative names from this one, as text. */
  resolve(relative: string): string {
    warnUrlParse();
    const r = new Url();
    r.parse(relative, false, true);
    return this.resolveObject(r).format();
  }

  /** The URL that relative names from this one (Node's Url.prototype.resolveObject). */
  resolveObject(rel: string | Url): Url {
    let relative = new Url();
    if (typeof rel === "string") relative.parse(rel as string, false, true);
    else relative = rel as Url;
    const result = new Url();
    copyUrl(result, this);
    result.hash = relative.hash;
    if (relative.href === "") {
      result.href = result.format();
      return result;
    }
    if (relative.slashes === true && !nonEmpty(relative.protocol)) {
      const keep = result.protocol;
      copyUrl(result, relative);
      result.protocol = keep;
      if (slashedProtocol(result.protocol) && nonEmpty(result.hostname) && !nonEmpty(result.pathname)) {
        result.pathname = "/";
        result.path = "/";
      }
      result.href = result.format();
      return result;
    }
    if (nonEmpty(relative.protocol) && !sameStr(relative.protocol, result.protocol)) {
      if (!slashedProtocol(relative.protocol)) {
        copyUrl(result, relative);
        result.href = result.format();
        return result;
      }
      result.protocol = relative.protocol;
      const rp = relative.protocol ?? "";
      if (!nonEmpty(relative.host) && rp !== "file" && rp !== "file:" && !hostlessProtocol(rp)) {
        const relPath = (relative.pathname ?? "").split("/");
        relative.host = null;
        while (relPath.length > 0) {
          const h = relPath.shift() as string;
          relative.host = h;
          if (h.length > 0) break;
        }
        if (!nonEmpty(relative.host)) relative.host = "";
        if (!nonEmpty(relative.hostname)) relative.hostname = "";
        if (relPath.length === 0 || relPath[0] !== "") relPath.unshift("");
        if (relPath.length < 2) relPath.unshift("");
        result.pathname = relPath.join("/");
      } else {
        result.pathname = relative.pathname;
      }
      result.search = relative.search;
      result.query = relative.query;
      result.host = nonEmpty(relative.host) ? relative.host : "";
      result.auth = relative.auth;
      result.hostname = nonEmpty(relative.hostname) ? relative.hostname : relative.host;
      result.port = relative.port;
      if (nonEmpty(result.pathname) || nonEmpty(result.search)) result.path = (result.pathname ?? "") + (result.search ?? "");
      if (result.slashes !== true) result.slashes = relative.slashes;
      result.href = result.format();
      return result;
    }

    const isSourceAbs = nonEmpty(result.pathname) && (result.pathname ?? "").charAt(0) === "/";
    const isRelAbs = nonEmpty(relative.host) || (nonEmpty(relative.pathname) && (relative.pathname ?? "").charAt(0) === "/");
    let mustEndAbs = isRelAbs || isSourceAbs || (nonEmpty(result.host) && nonEmpty(relative.pathname));
    const removeAllDots = mustEndAbs;
    let srcPath: string[] = nonEmpty(result.pathname) ? (result.pathname ?? "").split("/") : [];
    const relPath: string[] = nonEmpty(relative.pathname) ? (relative.pathname ?? "").split("/") : [];
    const noLeadingSlashes = nonEmpty(result.protocol) && !slashedProtocol(result.protocol);

    if (noLeadingSlashes) {
      result.hostname = "";
      result.port = null;
      if (nonEmpty(result.host)) {
        if (srcPath.length > 0 && srcPath[0] === "") srcPath[0] = result.host ?? "";
        else srcPath.unshift(result.host ?? "");
      }
      result.host = "";
      if (nonEmpty(relative.protocol)) {
        relative.hostname = null;
        relative.port = null;
        result.auth = null;
        if (nonEmpty(relative.host)) {
          if (relPath.length > 0 && relPath[0] === "") relPath[0] = relative.host ?? "";
          else relPath.unshift(relative.host ?? "");
        }
        relative.host = null;
      }
      mustEndAbs = mustEndAbs && ((relPath.length > 0 && relPath[0] === "") || (srcPath.length > 0 && srcPath[0] === ""));
    }

    if (isRelAbs) {
      if (relative.host !== null) {
        if (!sameStr(result.host, relative.host)) result.auth = null;
        result.host = relative.host;
        result.port = relative.port;
      }
      if (relative.hostname !== null) {
        if (!sameStr(result.hostname, relative.hostname)) result.auth = null;
        result.hostname = relative.hostname;
      }
      result.search = relative.search;
      result.query = relative.query;
      srcPath = relPath;
    } else if (relPath.length > 0) {
      if (srcPath.length > 0) srcPath.pop();
      for (const part of relPath) srcPath.push(part);
      result.search = relative.search;
      result.query = relative.query;
    } else if (relative.search !== null) {
      if (noLeadingSlashes) {
        const h: string | null = srcPath.length > 0 ? (srcPath.shift() as string) : null;
        result.hostname = h;
        result.host = h;
        if (h !== null && h.indexOf("@") > 0) {
          const authInHost = h.split("@");
          result.auth = authInHost.shift() as string;
          const rest = authInHost.shift() as string;
          result.host = rest;
          result.hostname = rest;
        }
      }
      result.search = relative.search;
      result.query = relative.query;
      if (result.pathname !== null || result.search !== null) result.path = (result.pathname ?? "") + (result.search ?? "");
      result.href = result.format();
      return result;
    }

    if (srcPath.length === 0) {
      result.pathname = null;
      result.path = nonEmpty(result.search) ? "/" + (result.search ?? "") : null;
      result.href = result.format();
      return result;
    }

    let last = srcPath[srcPath.length - 1];
    const hasTrailingSlash = ((nonEmpty(result.host) || nonEmpty(relative.host) || srcPath.length > 1) && (last === "." || last === "..")) || last === "";
    let up = 0;
    for (let i = srcPath.length - 1; i >= 0; i--) {
      last = srcPath[i];
      if (last === ".") {
        srcPath.splice(i, 1);
      } else if (last === "..") {
        srcPath.splice(i, 1);
        up++;
      } else if (up > 0) {
        srcPath.splice(i, 1);
        up--;
      }
    }
    if (!mustEndAbs && !removeAllDots) {
      while (up-- > 0) srcPath.unshift("..");
    }
    if (mustEndAbs && (srcPath.length === 0 || (srcPath[0] !== "" && srcPath[0].charAt(0) !== "/"))) srcPath.unshift("");
    if (hasTrailingSlash && !srcPath.join("/").endsWith("/")) srcPath.push("");
    const isAbsolute = srcPath.length > 0 && (srcPath[0] === "" || srcPath[0].charAt(0) === "/");
    if (noLeadingSlashes) {
      const h = isAbsolute ? "" : srcPath.length > 0 ? (srcPath.shift() as string) : "";
      result.hostname = h;
      result.host = h;
      if (h.indexOf("@") > 0) {
        const authInHost = h.split("@");
        result.auth = authInHost.shift() as string;
        const rest = authInHost.shift() as string;
        result.host = rest;
        result.hostname = rest;
      }
    }
    mustEndAbs = mustEndAbs || (nonEmpty(result.host) && srcPath.length > 0);
    if (mustEndAbs && !isAbsolute) srcPath.unshift("");
    if (srcPath.length === 0) {
      result.pathname = null;
      result.path = null;
    } else {
      result.pathname = srcPath.join("/");
    }
    if (result.pathname !== null || result.search !== null) result.path = (result.pathname ?? "") + (result.search ?? "");
    if (nonEmpty(relative.auth)) result.auth = relative.auth;
    if (result.slashes !== true) result.slashes = relative.slashes;
    result.href = result.format();
    return result;
  }
}

function copyUrl(to: Url, from: Url): void {
  to.protocol = from.protocol;
  to.slashes = from.slashes;
  to.auth = from.auth;
  to.host = from.host;
  to.port = from.port;
  to.hostname = from.hostname;
  to.hash = from.hash;
  to.search = from.search;
  to.query = from.query;
  to.pathname = from.pathname;
  to.path = from.path;
  to.href = from.href;
}

function legacyFormat(protocol0: string | null, slashes: boolean | null, auth0: string | null, host0: string | null, hostname: string | null,
  port: string | null, pathname0: string | null, search0: string | null, q: string, hash0: string | null): string {
  let auth = auth0 ?? "";
  if (auth.length > 0) auth = encodeAuth(auth) + "@";
  let protocol = protocol0 ?? "";
  if (protocol.length > 0 && !protocol.endsWith(":")) protocol += ":";
  let pathname = pathname0 ?? "";
  let hash = hash0 ?? "";
  let host = "";
  if (nonEmpty(host0)) {
    host = auth + (host0 ?? "");
  } else if (nonEmpty(hostname)) {
    const hn = hostname ?? "";
    host = auth + (hn.indexOf(":") !== -1 && !isIpv6Hostname(hn) ? "[" + hn + "]" : hn);
    if (nonEmpty(port)) host += ":" + (port ?? "");
  }
  let search = nonEmpty(search0) ? (search0 ?? "") : q.length > 0 ? "?" + q : "";
  if (pathname.indexOf("#") !== -1 || pathname.indexOf("?") !== -1) pathname = pathname.replaceAll("#", "%23").replaceAll("?", "%3F");
  if (slashes === true || slashedProtocol(protocol)) {
    if (slashes === true || host.length > 0) {
      if (pathname.length > 0 && pathname.charAt(0) !== "/") pathname = "/" + pathname;
      host = "//" + host;
    } else if (protocol.startsWith("file")) {
      host = "//";
    }
  }
  if (search.indexOf("#") !== -1) search = search.replaceAll("#", "%23");
  if (hash.length > 0 && hash.charAt(0) !== "#") hash = "#" + hash;
  if (search.length > 0 && search.charAt(0) !== "?") search = "?" + search;
  return protocol + host + pathname + search + hash;
}

/** Splits a URL into its parts, the legacy way (Node's url.parse; prefer new URL()). parseQueryString: query as an object. */
export function parse(url: string, parseQueryString: boolean = false, slashesDenoteHost: boolean = false): Url {
  warnUrlParse();
  const u = new Url();
  u.parse(url, parseQueryString, slashesDenoteHost);
  return u;
}

/** The URL as text: from a string (parsed and written back), a WHATWG URL (with options), or the parts of one. */
export function format(urlObject: string | URL | UrlObject, options: URLFormatOptions | null = null): string {
  if (typeof urlObject === "string") return parse(urlObject as string).format();
  if (urlObject instanceof URL) {
    const u = urlObject as URL;
    let fragment = true;
    let search = true;
    let auth = true;
    if (options !== null) {
      if (options.fragment !== null && options.fragment !== undefined) fragment = options.fragment as boolean;
      if (options.search !== null && options.search !== undefined) search = options.search as boolean;
      if (options.auth !== null && options.auth !== undefined) auth = options.auth as boolean;
    }
    return u.__format(fragment, search, auth);
  }
  const o = urlObject as UrlObject;
  const port: string | number | null | undefined = o.port;
  let ports: string | null = null;
  if (typeof port === "number") ports = String(port as number);
  else if (typeof port === "string") ports = port as string;
  const sl = o.slashes;
  const query = o.query;
  let q = "";
  if (query !== null && query !== undefined && typeof query !== "string") q = legacyQsStringify(query as LegacyQueryInput);
  return legacyFormat(o.protocol ?? null, sl === true ? true : null, o.auth ?? null, o.host ?? null, o.hostname ?? null, ports, o.pathname ?? null,
    o.search ?? null, q, o.hash ?? null);
}

/** The URL that to names relative to from, the legacy way (Node's url.resolve). */
export function resolve(from: string, to: string): string {
  return parse(from, false, true).resolve(to);
}
