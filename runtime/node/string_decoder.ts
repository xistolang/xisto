// xisto:string_decoder (node:string_decoder): StringDecoder, which turns chunks of bytes into text without splitting
// a character that falls across two chunks: the bytes of an incomplete character wait for the next write().
// Encodings: utf8 (utf-8), utf16le (ucs2, ucs-2, utf-16le), latin1 (binary), base64, base64url, hex and ascii. The
// algorithm is Node's (src/string_decoder.cc): an invalid UTF-8 sequence becomes U+FFFD as Buffer.toString does, and
// end() turns an incomplete UTF-8 character into one U+FFFD.
// Differences from Node (docs/docs/guides/node-compatibility.md): write() and end() take bytes (a Buffer or a
// Uint8Array; Node also takes a string); lastChar, lastNeed and lastTotal are not there.

function normalize(enc: string): string {
  const e = enc.toLowerCase();
  if (e === "utf8" || e === "utf-8" || e === "") return "utf8";
  if (e === "utf16le" || e === "utf-16le" || e === "ucs2" || e === "ucs-2") return "utf16le";
  if (e === "latin1" || e === "binary") return "latin1";
  if (e === "base64" || e === "base64url" || e === "hex" || e === "ascii") return e;
  return "";
}

// UTF-8 as the WHATWG decoder reads it (V8's, Node's): each maximal invalid subpart one U+FFFD; the valid runs
// between them are copied as they are
function decodeUtf8(b: Buffer, start: int, end: int): string {
  let out = "";
  let i: int = start;
  let run: int = start; // (the start of the valid bytes not copied yet)
  while (i < end) {
    const c = b[i];
    if (c < 0x80) {
      i++;
      continue;
    }
    let need: int = 0;
    let lower: int = 0x80;
    let upper: int = 0xbf;
    if (c >= 0xc2 && c <= 0xdf) need = 1;
    else if (c >= 0xe0 && c <= 0xef) {
      need = 2;
      if (c === 0xe0) lower = 0xa0;
      if (c === 0xed) upper = 0x9f;
    } else if (c >= 0xf0 && c <= 0xf4) {
      need = 3;
      if (c === 0xf0) lower = 0x90;
      if (c === 0xf4) upper = 0x8f;
    }
    let j: int = i + 1;
    let ok = need > 0;
    for (let k: int = 0; k < need; k++) {
      if (j >= end || b[j] < lower || b[j] > upper) {
        ok = false;
        break;
      }
      lower = 0x80;
      upper = 0xbf;
      j++;
    }
    if (!ok) {
      if (run < i) out += b.toString("utf8", run, i);
      out += "\ufffd";
      run = j;
    }
    i = j;
  }
  if (run === start) return b.toString("utf8", start, end);
  if (run < end) out += b.toString("utf8", run, end);
  return out;
}

/** Decodes a stream of bytes in an encoding, keeping an incomplete character for the next chunk. */
export class StringDecoder {
  /** The encoding, normalized (utf8, utf16le, latin1, base64, base64url, hex or ascii). */
  readonly encoding: string;
  private pending: Buffer = Buffer.alloc(4);
  private buffered: int = 0;
  private missing: int = 0;

  constructor(encoding: string = "utf8") {
    const e = normalize(encoding);
    if (e === "") {
      const err = new TypeError(`Unknown encoding: ${encoding}`);
      throw err;
    }
    this.encoding = e;
  }

  private text(b: Buffer, start: int, end: int): string {
    if (start >= end) return "";
    if (this.encoding === "utf16le") {
      // two bytes per UTF-16 code unit, little-endian (an odd last byte ignored, as Buffer.toString)
      let out = "";
      let i: int = start;
      while (i + 1 < end) {
        const u = b[i] + b[i + 1] * 256;
        i += 2;
        if (u >= 0xd800 && u <= 0xdbff && i + 1 < end) {
          const v = b[i] + b[i + 1] * 256;
          if (v >= 0xdc00 && v <= 0xdfff) {
            out += String.fromCharCode(u, v); // (a pair: one code point)
            i += 2;
            continue;
          }
        }
        out += String.fromCharCode(u);
      }
      return out;
    }
    if (this.encoding === "utf8") {
      return decodeUtf8(b, start, end);
    }
    return b.toString(this.encoding as BufferEncoding, start, end);
  }

  /** The text of the complete characters in the bytes (with those left from the previous write before them). */
  write(buf: Uint8Array): string {
    const data = Buffer.from(buf);
    let pos: int = 0;
    let nread: int = data.length;
    let prepend = "";
    if (this.missing > 0) {
      if (this.encoding === "utf8") {
        // a byte that is not a continuation ends the incomplete character here: it starts a new one
        for (let i: int = 0; i < nread && i < this.missing; i++) {
          if ((data[i] & 0xc0) !== 0x80) {
            this.missing = 0;
            for (let k: int = 0; k < i; k++) this.pending[this.buffered + k] = data[k];
            this.buffered += i;
            pos += i;
            nread -= i;
            break;
          }
        }
      }
      const found = nread < this.missing ? nread : this.missing;
      for (let k: int = 0; k < found; k++) this.pending[this.buffered + k] = data[pos + k];
      pos += found;
      nread -= found;
      this.missing -= found;
      this.buffered += found;
      if (this.missing === 0) {
        if (this.encoding === "utf16le" && nread > 0) {
          // decoded with the rest: half of a surrogate pair cannot be a string of its own here (UTF-8 strings)
          const joined = Buffer.concat([this.pending.subarray(0, this.buffered), data.subarray(pos, pos + nread)]);
          this.buffered = 0;
          return this.write(joined);
        }
        prepend = this.text(this.pending, 0, this.buffered);
        this.buffered = 0;
      }
    }
    if (nread === 0) return prepend;
    // an incomplete character at the end: kept for the next write
    const end: int = pos + nread;
    if (this.encoding === "utf8" && (data[end - 1] & 0x80) !== 0) {
      for (let i: int = end - 1; ; i--) {
        this.buffered++;
        if ((data[i] & 0xc0) === 0x80) {
          if (this.buffered >= 4 || i === pos) {
            this.buffered = 0;
            break;
          }
        } else {
          const lead = data[i];
          if ((lead & 0xe0) === 0xc0) this.missing = 2;
          else if ((lead & 0xf0) === 0xe0) this.missing = 3;
          else if ((lead & 0xf8) === 0xf0) this.missing = 4;
          else {
            this.buffered = 0;
            break;
          }
          if (this.buffered >= this.missing) {
            this.missing = 0;
            this.buffered = 0;
          }
          this.missing -= this.buffered;
          break;
        }
      }
    } else if (this.encoding === "utf16le") {
      if (nread % 2 === 1) {
        this.buffered = 1;
        this.missing = 1;
      } else if ((data[end - 1] & 0xfc) === 0xd8) {
        this.buffered = 2;
        this.missing = 2;
      }
    } else if (this.encoding === "base64" || this.encoding === "base64url") {
      this.buffered = nread % 3;
      if (this.buffered > 0) this.missing = 3 - this.buffered;
    }
    for (let k: int = 0; k < this.buffered; k++) this.pending[k] = data[end - this.buffered + k];
    return prepend + this.text(data, pos, end - this.buffered);
  }

  /** The text of what is left (an incomplete UTF-8 character: U+FFFD), after write(buf) when bytes are given. */
  end(buf: Uint8Array | null = null): string {
    const head = buf !== null ? this.write(buf) : "";
    let tail = "";
    if (this.buffered > 0) tail = this.encoding === "utf8" ? "�" : this.text(this.pending, 0, this.buffered);
    this.buffered = 0;
    this.missing = 0;
    return head + tail;
  }
}
