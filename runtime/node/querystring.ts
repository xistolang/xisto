// xisto:querystring (node:querystring): parse/decode, stringify/encode, escape, unescape, unescapeBuffer.
// The same algorithm as Node's (lib/querystring.js): sep and eq of any length, maxKeys (1000 by default, 0 for no
// limit), '+' read as a space, %XX sequences decoded as UTF-8 (a malformed one kept as written, bytes that are not
// UTF-8 read as U+FFFD), a key given more than once collected into an array.
// Differences from Node (docs/docs/guides/node-compatibility.md):
//   - parse() returns a Record<string, any> (each value a string or a string[]); console.log prints it as a plain
//     object, without Node's [Object: null prototype] prefix;
//   - stringify() reads the object's own fields through any (the values a string, number, boolean, bigint or an
//     array of those; anything else is written as an empty value, as Node does).

/** What parse() gives: each key's value, a string or (a key given more than once) a string[]. */
export type ParsedUrlQuery = Record<string, any>;

/** parse()'s options: maxKeys (0: no limit) and the function that decodes keys and values. */
export interface ParseOptions {
  maxKeys?: number;
  decodeURIComponent?: (s: string) => string;
}

/** stringify()'s options: the function that encodes keys and values. */
export interface StringifyOptions {
  encodeURIComponent?: (s: string) => string;
}

function hexVal(c: number): int {
  if (c >= 48 && c <= 57) return Math.trunc(c - 48);
  if (c >= 65 && c <= 70) return Math.trunc(c - 55);
  if (c >= 97 && c <= 102) return Math.trunc(c - 87);
  return -1;
}

/** The bytes of a percent-encoded string: each valid %XX one byte, every other character its low byte ('+' a space
 *  when decodeSpaces), as Node's querystring.unescapeBuffer. */
export function unescapeBuffer(s: string, decodeSpaces: boolean = false): Buffer {
  const len: int = s.length;
  const out = Buffer.alloc(len);
  let index: int = 0;
  let outIndex: int = 0;
  const maxLength: int = len - 2;
  while (index < len) {
    let c = s.charCodeAt(index);
    if (c === 43 && decodeSpaces) {
      out[outIndex++] = 32;
      index++;
      continue;
    }
    if (c === 37 && index < maxLength) {
      c = s.charCodeAt(++index);
      const hi = hexVal(c);
      if (hi < 0) {
        out[outIndex++] = 37;
        continue;
      }
      const lo = hexVal(s.charCodeAt(++index));
      if (lo < 0) {
        out[outIndex++] = 37;
        index--;
      } else {
        c = hi * 16 + lo;
      }
    }
    out[outIndex++] = Math.trunc(c) & 255;
    index++;
  }
  return out.subarray(0, outIndex);
}

// decodeURIComponent would accept the string: each % starts %XX, and the escaped bytes are well-formed UTF-8
function wellFormed(s: string): boolean {
  const n: int = s.length;
  let i: int = 0;
  const byteAt = (at: int): int => {
    if (at + 2 >= n + 0 || s.charCodeAt(at) !== 37) return -1;
    const hi = hexVal(s.charCodeAt(at + 1));
    const lo = hexVal(s.charCodeAt(at + 2));
    return hi < 0 || lo < 0 ? -1 : hi * 16 + lo;
  };
  while (i < n) {
    if (s.charCodeAt(i) !== 37) {
      i++;
      continue;
    }
    if (i + 2 >= n) return false;
    const b = byteAt(i);
    if (b < 0) return false;
    i += 3;
    if (b < 0x80) continue;
    let more: int = 0;
    let min: int = 0;
    let cp: int = 0;
    if (b >= 0xc2 && b <= 0xdf) {
      more = 1;
      min = 0x80;
      cp = b & 0x1f;
    } else if (b >= 0xe0 && b <= 0xef) {
      more = 2;
      min = 0x800;
      cp = b & 0x0f;
    } else if (b >= 0xf0 && b <= 0xf4) {
      more = 3;
      min = 0x10000;
      cp = b & 0x07;
    } else return false;
    for (let k: int = 0; k < more; k++) {
      if (i + 2 >= n) return false;
      const c = byteAt(i);
      if (c < 0x80 || c > 0xbf) return false;
      cp = cp * 64 + (c & 0x3f);
      i += 3;
    }
    if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
  }
  return true;
}

/** decodeURIComponent, and for a string it rejects the bytes of unescapeBuffer read as UTF-8 (as Node). */
export function unescape(s: string, decodeSpaces: boolean = false): string {
  if (wellFormed(s)) return decodeURIComponent(s);
  return unescapeBuffer(s, decodeSpaces).toString("utf8");
}

/** encodeURIComponent: every character but A-Z a-z 0-9 - . _ ~ ! ' ( ) * percent-encoded as UTF-8. */
export function escape(s: string): string {
  return encodeURIComponent(s);
}

function primitiveText(v: any): string {
  const t = typeof v;
  if (t === "string") return v as string;
  if (t === "number") {
    const n = v as number;
    return isFinite(n) ? `${n}` : "";
  }
  if (t === "bigint") return `${v}`;
  if (t === "boolean") return (v as boolean) ? "true" : "false";
  return "";
}

/** The object's fields as key=value pairs joined by sep (an array value: one pair per element). */
export function stringify(obj: any, sep: string | null = null, eq: string | null = null, options: StringifyOptions | null = null): string {
  const s = sep !== null && sep !== undefined && sep !== "" ? sep : "&";
  const e = eq !== null && eq !== undefined && eq !== "" ? eq : "=";
  let encoder: (x: string) => string = escape;
  if (options !== null && options.encodeURIComponent !== null && options.encodeURIComponent !== undefined) {
    encoder = options.encodeURIComponent as (x: string) => string;
  }
  if (obj === null || typeof obj !== "object" || Array.isArray(obj)) {
    if (obj === null || typeof obj !== "object") return "";
  }
  let out = "";
  let first = true;
  for (const k of Object.keys(obj)) {
    const v: any = obj[k];
    const ks = encoder(k) + e;
    if (Array.isArray(v)) {
      const n: int = v.length as int;
      for (let i: int = 0; i < n; i++) {
        if (!first) out += s;
        first = false;
        out += ks + encoder(primitiveText(v[i]));
      }
    } else {
      if (!first) out += s;
      first = false;
      out += ks + encoder(primitiveText(v));
    }
  }
  return out;
}

/** The same as stringify(). */
export function encode(obj: any, sep: string | null = null, eq: string | null = null, options: StringifyOptions | null = null): string {
  return stringify(obj, sep, eq, options);
}

class Collector {
  order: string[] = [];
  vals: Map<string, string[]> = new Map<string, string[]>();
  add(key: string, value: string): void {
    const found = this.vals.get(key);
    if (found !== null) found.push(value);
    else {
      this.order.push(key);
      this.vals.set(key, [value]);
    }
  }
  result(): ParsedUrlQuery {
    const obj: ParsedUrlQuery = {};
    for (const k of this.order) {
      const v = this.vals.get(k) as string[];
      if (v.length === 1) obj[k] = v[0];
      else obj[k] = v;
    }
    return obj;
  }
}

function isHex(c: number): boolean {
  return hexVal(c) >= 0;
}

/** The pairs of a query string (without the '?'): key=value separated by sep. */
export function parse(qs: string, sep: string | null = null, eq: string | null = null, options: ParseOptions | null = null): ParsedUrlQuery {
  const col = new Collector();
  if (qs.length === 0) return col.result();
  const sepS = sep !== null && sep !== undefined && sep !== "" ? sep : "&";
  const eqS = eq !== null && eq !== undefined && eq !== "" ? eq : "=";
  const sepLen: int = sepS.length;
  const eqLen: int = eqS.length;
  let pairs: int = 1000;
  let decoder: (x: string) => string = (x: string): string => unescape(x, false);
  let customDecode = false;
  if (options !== null) {
    const mk = options.maxKeys;
    if (mk !== null && mk !== undefined) pairs = mk > 0 ? (Math.floor(mk) as int) : -1;
    const dec = options.decodeURIComponent;
    if (dec !== null && dec !== undefined) {
      decoder = dec as (x: string) => string;
      customDecode = true;
    }
  }
  const decodeStr = (s: string): string => {
    if (!customDecode) return unescape(s, true);
    try {
      return decoder(s);
    } catch (e) {
      return unescape(s, true);
    }
  };
  const addKeyVal = (key: string, value: string, keyEncoded: boolean, valEncoded: boolean): void => {
    const k = key.length > 0 && keyEncoded ? decodeStr(key) : key;
    const v = value.length > 0 && valEncoded ? decodeStr(value) : value;
    col.add(k, v);
  };
  let lastPos: int = 0;
  let sepIdx: int = 0;
  let eqIdx: int = 0;
  let key = "";
  let value = "";
  let keyEncoded = customDecode;
  let valEncoded = customDecode;
  const plusChar = customDecode ? "%20" : " ";
  let encodeCheck: int = 0;
  const n: int = qs.length;
  for (let i: int = 0; i < n; ++i) {
    const code = qs.charCodeAt(i);
    if (code === sepS.charCodeAt(sepIdx)) {
      if (++sepIdx === sepLen) {
        // the key/value pair separator
        const end = i - sepIdx + 1;
        if (eqIdx < eqLen) {
          if (lastPos < end) {
            key += qs.slice(lastPos, end);
          } else if (key.length === 0) {
            // an empty piece between separators
            if (--pairs === 0) return col.result();
            lastPos = i + 1;
            sepIdx = 0;
            eqIdx = 0;
            continue;
          }
        } else if (lastPos < end) {
          value += qs.slice(lastPos, end);
        }
        addKeyVal(key, value, keyEncoded, valEncoded);
        if (--pairs === 0) return col.result();
        keyEncoded = customDecode;
        valEncoded = customDecode;
        key = "";
        value = "";
        encodeCheck = 0;
        lastPos = i + 1;
        sepIdx = 0;
        eqIdx = 0;
      }
    } else {
      sepIdx = 0;
      if (eqIdx < eqLen) {
        if (code === eqS.charCodeAt(eqIdx)) {
          if (++eqIdx === eqLen) {
            // the key/value separator
            const end = i - eqIdx + 1;
            if (lastPos < end) key += qs.slice(lastPos, end);
            encodeCheck = 0;
            lastPos = i + 1;
          }
          continue;
        } else {
          eqIdx = 0;
          if (!keyEncoded) {
            // a valid %XX in the key: it needs decoding
            if (code === 37) {
              encodeCheck = 1;
              continue;
            } else if (encodeCheck > 0) {
              if (isHex(code)) {
                if (++encodeCheck === 3) keyEncoded = true;
                continue;
              } else {
                encodeCheck = 0;
              }
            }
          }
        }
        if (code === 43) {
          if (lastPos < i) key += qs.slice(lastPos, i);
          key += plusChar;
          lastPos = i + 1;
          continue;
        }
      }
      if (code === 43) {
        if (lastPos < i) value += qs.slice(lastPos, i);
        value += plusChar;
        lastPos = i + 1;
      } else if (!valEncoded) {
        if (code === 37) {
          encodeCheck = 1;
        } else if (encodeCheck > 0) {
          if (isHex(code)) {
            if (++encodeCheck === 3) valEncoded = true;
          } else {
            encodeCheck = 0;
          }
        }
      }
    }
  }
  // what is left after the last separator
  if (lastPos < n) {
    if (eqIdx < eqLen) key += qs.slice(lastPos);
    else if (sepIdx < sepLen) value += qs.slice(lastPos);
  } else if (eqIdx === 0 && key.length === 0) {
    return col.result();
  }
  addKeyVal(key, value, keyEncoded, valEncoded);
  return col.result();
}

/** The same as parse(). */
export function decode(qs: string, sep: string | null = null, eq: string | null = null, options: ParseOptions | null = null): ParsedUrlQuery {
  return parse(qs, sep, eq, options);
}

/** The module as one object: import querystring from "node:querystring". */
export class QueryString {
  parse(qs: string, sep: string | null = null, eq: string | null = null, options: ParseOptions | null = null): ParsedUrlQuery {
    return parse(qs, sep, eq, options);
  }
  decode(qs: string, sep: string | null = null, eq: string | null = null, options: ParseOptions | null = null): ParsedUrlQuery {
    return parse(qs, sep, eq, options);
  }
  stringify(obj: any, sep: string | null = null, eq: string | null = null, options: StringifyOptions | null = null): string {
    return stringify(obj, sep, eq, options);
  }
  encode(obj: any, sep: string | null = null, eq: string | null = null, options: StringifyOptions | null = null): string {
    return stringify(obj, sep, eq, options);
  }
  escape(s: string): string {
    return escape(s);
  }
  unescape(s: string, decodeSpaces: boolean = false): string {
    return unescape(s, decodeSpaces);
  }
  unescapeBuffer(s: string, decodeSpaces: boolean = false): Buffer {
    return unescapeBuffer(s, decodeSpaces);
  }
}

const querystring: QueryString = new QueryString();
export default querystring;
