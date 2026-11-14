// Buffer: writeIntLE/BE of 6 bytes with a fraction (Node's writeU_Int48), ucs2 indexOf of a needle under 2 bytes
const b = Buffer.alloc(6);
b.writeIntLE(-0.5, 0, 6);
console.log(b);
const c = Buffer.alloc(6);
c.writeIntBE(-1.5, 0, 6);
console.log(c);
const u = Buffer.from("abc", "ucs2");
console.log(u.indexOf("", 0, "ucs2"), u.indexOf(Buffer.from([0x61]), 0, "ucs2"), u.indexOf("b", 0, "ucs2"));
