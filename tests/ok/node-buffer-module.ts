// node:buffer as a module ("buffer" too): Buffer, isUtf8, isAscii, atob, btoa, kMaxLength, kStringMaxLength, constants,
// INSPECT_MAX_BYTES; and a small length-prefixed protocol written and parsed the way a database driver does it
import { Buffer, isUtf8, isAscii, atob, btoa, kMaxLength, kStringMaxLength, constants, INSPECT_MAX_BYTES } from "node:buffer";
import { isAscii as isAscii2 } from "buffer";

console.log(Buffer.from("hi"), isAscii2(Buffer.from("yo")), kMaxLength, kStringMaxLength, constants, INSPECT_MAX_BYTES, constants.MAX_LENGTH);
console.log(isUtf8(Buffer.from([0xc3, 0xa9])), isUtf8(Buffer.from([0xff])), isUtf8(new Uint8Array([0xed, 0xa0, 0x80])), isUtf8(Buffer.alloc(0)));
console.log(isAscii(Buffer.from("abc")), isAscii(Buffer.from("é")), atob("aGk="), btoa("hi"), atob(btoa("ÿ")) === "ÿ");

// a message: type (1 byte), length (int32 BE, itself included), then fields
class Writer {
  private buf: Buffer = Buffer.alloc(16);
  private pos: int = 5;
  private grow(n: int): void {
    if (this.pos + n <= this.buf.length) return;
    const next = Buffer.alloc(Math.max(this.buf.length * 2, this.pos + n));
    this.buf.copy(next, 0, 0, this.pos);
    this.buf = next;
  }
  int16(v: int): Writer {
    this.grow(2);
    this.pos = this.buf.writeInt16BE(v, this.pos);
    return this;
  }
  int32(v: int): Writer {
    this.grow(4);
    this.pos = this.buf.writeInt32BE(v, this.pos);
    return this;
  }
  double(v: number): Writer {
    this.grow(8);
    this.pos = this.buf.writeDoubleBE(v, this.pos);
    return this;
  }
  big(v: bigint): Writer {
    this.grow(8);
    this.pos = this.buf.writeBigInt64BE(v, this.pos);
    return this;
  }
  cstring(s: string): Writer {
    const n = Buffer.byteLength(s);
    this.grow(n + 1);
    this.buf.write(s, this.pos);
    this.pos += n;
    this.buf[this.pos++] = 0;
    return this;
  }
  flush(type: int): Buffer {
    this.buf.writeUInt8(type, 0);
    this.buf.writeInt32BE(this.pos - 1, 1);
    return this.buf.subarray(0, this.pos);
  }
}
const msg = new Writer().int16(-2).int32(123456).cstring("héllo").double(-1.25).big(-9007199254740993n).cstring("").flush(0x44);
console.log(msg, msg.length);

const packet = Buffer.concat([msg, Buffer.from([0x5a, 0, 0, 0, 5, 0x49])]);
let off: int = 0;
while (off < packet.length) {
  const type = String.fromCharCode(packet.readUInt8(off));
  const len = packet.readInt32BE(off + 1);
  const body = packet.subarray(off + 5, off + 1 + len);
  if (type === "D") {
    let p = 0;
    const a = body.readInt16BE(p); p += 2;
    const b = body.readInt32BE(p); p += 4;
    const z = body.indexOf(0, p);
    const s = body.toString("utf8", p, z); p = z + 1;
    const d = body.readDoubleBE(p); p += 8;
    const g = body.readBigInt64BE(p); p += 8;
    console.log(type, len, a, b, s, d, g, body.indexOf(0, p) === p);
  } else {
    console.log(type, len, body.toString("latin1"));
  }
  off += 1 + len;
}
