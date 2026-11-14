// node:buffer: readUInt8 ... readBigInt64BE, readFloat/Double, readUIntLE(offset, byteLength) and every write*, with
// Node's RangeErrors and codes for offsets and values out of range

// integers and floats read and written at an offset, both byte orders
const b = Buffer.from([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0xff, 0xfe, 0x80, 0x00]);
console.log(b.readUInt8(0), b.readUint8(8), b.readInt8(8), b.readInt8(10));
console.log(b.readUInt16LE(0), b.readUInt16BE(0), b.readInt16LE(8), b.readInt16BE(8), b.readUint16LE(1), b.readUint16BE(1));
console.log(b.readUInt32LE(0), b.readUInt32BE(0), b.readInt32LE(8), b.readInt32BE(8), b.readUint32LE(4), b.readUint32BE(8));
console.log(b.readBigUInt64LE(0), b.readBigUInt64BE(0), b.readBigInt64LE(4), b.readBigInt64BE(4), b.readBigUint64LE(4), b.readBigUint64BE(4));
console.log(b.readUIntLE(0, 3), b.readUIntBE(0, 6), b.readIntLE(7, 3), b.readIntBE(8, 2), b.readUintLE(8, 1), b.readUintBE(6, 5), b.readIntLE(6, 6));
console.log(b.readFloatLE(0), b.readFloatBE(0), b.readDoubleLE(0), b.readDoubleBE(0));
console.log(b.readUInt8(), b.readUInt16BE(), b.readInt32LE(), b.readDoubleBE(), b.readBigUInt64LE());

const w = Buffer.alloc(16);
console.log(w.writeUInt8(255, 0), w.writeInt8(-2, 1), w.writeUInt16LE(0xbeef, 2), w.writeUInt16BE(0xbeef, 4), w);
console.log(w.writeInt16LE(-2, 6), w.writeInt16BE(-300, 8), w.writeUInt32LE(0xdeadbeef, 10), w);
console.log(w.writeUInt32BE(0xdeadbeef, 0), w.writeInt32LE(-1, 4), w.writeInt32BE(-123456789, 8), w);
console.log(w.writeFloatLE(1.5, 0), w.writeFloatBE(-0.1, 4), w.writeDoubleLE(Math.PI, 8), w);
console.log(w.readFloatLE(0), w.readFloatBE(4), w.readDoubleLE(8));
console.log(w.writeDoubleBE(-1e300, 0), w.writeBigUInt64LE(0xfedcba9876543210n, 8), w);
console.log(w.writeBigInt64BE(-2n, 0), w.writeBigInt64LE(-9223372036854775808n, 8), w.readBigInt64BE(0), w.readBigInt64LE(8), w.readBigUInt64LE(8));
console.log(w.writeUIntLE(0x123456, 0, 3), w.writeUIntBE(0x123456789abc, 3, 6), w.writeIntLE(-5, 9, 2), w.writeIntBE(-0x123456789a, 11, 5), w);
console.log(w.readUIntLE(0, 3), w.readUIntBE(3, 6), w.readIntLE(9, 2), w.readIntBE(11, 5));
console.log(w.writeUint8(1), w.writeUint16LE(2), w.writeUint32BE(3), w.writeUintLE(4, 0, 1), w.writeUintBE(5, 0, 1), w.writeBigUint64LE(6n), w.writeBigUint64BE(7n));
console.log(w.writeUInt8(1.9, 0), w.writeInt8(-1.5, 1), w.writeUInt32LE(4294967295, 2), w.writeFloatLE(1e40, 6), w.writeFloatBE(NaN, 10), w);
console.log(w.readFloatLE(6), w.readFloatBE(10), w.writeDoubleLE(-0, 0), w.readDoubleLE(0), w.writeDoubleLE(Infinity, 0), w.readDoubleLE(0));
let o = 0;
const p = Buffer.alloc(11);
o = p.writeUInt8(7, o);
o = p.writeUInt16BE(513, o);
o = p.writeInt32LE(-42, o);
o = p.writeFloatBE(2.5, o);
console.log(o, p);

function tryIt(name: string, f: () => void): void {
  try {
    f();
    console.log(name, "ok");
  } catch (e: any) {
    console.log(name, e.name, e.code, e.message);
  }
}
const s = Buffer.alloc(4);
tryIt("u8 4", () => { s.readUInt8(4); });
tryIt("u8 -1", () => { s.readUInt8(-1); });
tryIt("u16 3", () => { s.readUInt16LE(3); });
tryIt("i32 1", () => { s.readInt32BE(1); });
tryIt("f64 0", () => { s.readDoubleLE(0); });
tryIt("big 0", () => { s.readBigUInt64LE(0); });
tryIt("uintle 0 5", () => { s.readUIntLE(0, 5); });
tryIt("uintle 0 7", () => { s.readUIntLE(0, 7); });
tryIt("intbe 0 0", () => { s.readIntBE(0, 0); });
tryIt("intbe 2 3", () => { s.readIntBE(2, 3); });
tryIt("w u8 256", () => { s.writeUInt8(256, 0); });
tryIt("w u8 -1", () => { s.writeUInt8(-1, 0); });
tryIt("w i8 128", () => { s.writeInt8(128, 0); });
tryIt("w i8 -129", () => { s.writeInt8(-129); });
tryIt("w u16 65536", () => { s.writeUInt16LE(65536); });
tryIt("w i16 40000", () => { s.writeInt16BE(40000); });
tryIt("w u32 big", () => { s.writeUInt32LE(4294967296); });
tryIt("w u32 neg", () => { s.writeUInt32BE(-1); });
tryIt("w i32 big", () => { s.writeInt32LE(2147483648); });
tryIt("w i32 1.5", () => { s.writeInt32LE(2147483647.5); });
tryIt("w u8 off", () => { s.writeUInt8(1, 4); });
tryIt("w u16 off", () => { s.writeUInt16LE(1, 3); });
tryIt("w u32 off", () => { s.writeUInt32LE(1, 1); });
tryIt("w u32 off -1", () => { s.writeUInt32LE(1, -1); });
tryIt("w f32 off", () => { s.writeFloatLE(1, 1); });
tryIt("w f64 off", () => { s.writeDoubleLE(1, 0); });
tryIt("w big", () => { s.writeBigUInt64LE(1n, 0); });
tryIt("w big range", () => { Buffer.alloc(8).writeBigUInt64LE(-1n, 0); });
tryIt("w big range2", () => { Buffer.alloc(8).writeBigUInt64BE(18446744073709551616n, 0); });
tryIt("w bigi range", () => { Buffer.alloc(8).writeBigInt64LE(9223372036854775808n, 0); });
tryIt("w uintle 3 range", () => { s.writeUIntLE(16777216, 0, 3); });
tryIt("w uintle 5 range", () => { Buffer.alloc(8).writeUIntLE(1099511627776, 0, 5); });
tryIt("w intle 6 range", () => { Buffer.alloc(8).writeIntLE(140737488355328, 0, 6); });
tryIt("w intbe 1 range", () => { s.writeIntBE(-129, 0, 1); });
tryIt("w uintle 7", () => { s.writeUIntLE(1, 0, 7); });
tryIt("w uintle off", () => { s.writeUIntLE(1, 2, 3); });
tryIt("w u8 big value", () => { s.writeUInt8(9007199254740991, 0); });
tryIt("empty", () => { Buffer.alloc(0).readUInt8(0); });
tryIt("short", () => { Buffer.alloc(1).readUInt16LE(0); });
tryIt("short w", () => { Buffer.alloc(1).writeUInt32BE(0, 0); });
