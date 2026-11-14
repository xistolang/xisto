// node:buffer: alloc (with a fill string, bytes, encoding), from, concat(list, totalLength), compare, equals, isEncoding,
// byteLength, copy, fill, indexOf/lastIndexOf/includes, views, swap16/32/64, toJSON, iteration, write, printing

// creating buffers
console.log(Buffer.alloc(4), Buffer.alloc(5, 1), Buffer.alloc(5, "ab"), Buffer.alloc(6, "aGk=", "base64"), Buffer.alloc(3, "4142", "hex"));
console.log(Buffer.alloc(4, Buffer.from([1, 2, 3])), Buffer.alloc(3, ""), Buffer.alloc(2, 257));
console.log(Buffer.allocUnsafe(3).fill(7), Buffer.allocUnsafeSlow(2).fill(0).length);
console.log(Buffer.from([1, 2, 300, -1]), Buffer.from("héllo"), Buffer.from("68656c6c6f", "hex"), Buffer.from(Buffer.from("x")));
const ab = new Uint8Array([10, 20, 30, 40, 50]);
const view = Buffer.from(ab.buffer, 1, 3);
console.log(view, view.byteOffset, view.length);
console.log(Buffer.concat([Buffer.from("ab"), Buffer.from("cd")]), Buffer.concat([Buffer.from("ab"), Buffer.from("cd")], 3), Buffer.concat([Buffer.from("ab")], 5));
console.log(Buffer.concat([]), Buffer.concat([Buffer.from([1, 2]), Buffer.from([3])]));

// comparing
const a = Buffer.from("abc"), b2 = Buffer.from("abd"), c = Buffer.from("ab");
console.log(Buffer.compare(a, b2), Buffer.compare(b2, a), Buffer.compare(a, a), Buffer.compare(c, a), Buffer.compare(a, c));
console.log(a.compare(b2), a.compare(c), a.compare(b2, 0, 2, 0, 2), a.compare(b2, 2), a.compare(b2, 1, 3, 1));
console.log(a.equals(Buffer.from("abc")), a.equals(b2), a.equals(new Uint8Array([97, 98, 99])));
const list = [b2, a, c, Buffer.from("a")];
list.sort((x, y) => Buffer.compare(x, y));
console.log(list);
console.log(Buffer.isBuffer(a), Buffer.isBuffer(new Uint8Array(1)), Buffer.isEncoding("utf8"), Buffer.isEncoding("UTF-8"), Buffer.isEncoding("hex"), Buffer.isEncoding("ucs-2"), Buffer.isEncoding("nope"), Buffer.isEncoding(""));
console.log(Buffer.byteLength("héllo"), Buffer.byteLength("héllo", "utf8"), Buffer.byteLength("aGVsbG8=", "base64"), Buffer.byteLength("abcd", "hex"), Buffer.byteLength("hé", "ucs2"), Buffer.byteLength("hé", "latin1"), Buffer.byteLength("aGVsbG8", "base64url"));
console.log(Buffer.byteLength(Buffer.from("xyz")), Buffer.byteLength(new Uint8Array(7)));

// copy, fill
const dst = Buffer.alloc(8, ".");
console.log(Buffer.from("hello").copy(dst, 2), dst.toString());
console.log(Buffer.from("hello").copy(dst, 0, 3), dst.toString(), Buffer.from("hello").copy(dst, 6, 1, 4), dst.toString());
console.log(Buffer.from("hello").copy(dst), Buffer.from("hello").copy(dst, 8), Buffer.from("hello").copy(dst, 0, 5));
const self = Buffer.from("abcdef");
self.copy(self, 2, 0, 4);
console.log(self.toString());
const f = Buffer.alloc(10);
console.log(f.fill("ab"), f.fill(0).fill("xyz", 2, 7), f.fill("e8", 0, 4, "hex"), f.fill(Buffer.from([1, 2, 3]), 6));
console.log(Buffer.alloc(5).fill("é"), Buffer.alloc(4).fill(-1), Buffer.alloc(4).fill(256 + 3, 1, 3));

// searching
const h = Buffer.from("this is a buffer, a buffer");
console.log(h.indexOf("buffer"), h.indexOf("buffer", 11), h.indexOf("buffer", -5), h.indexOf(Buffer.from("is")), h.indexOf(97), h.indexOf("zzz"), h.indexOf(""), h.indexOf("", 100));
console.log(h.lastIndexOf("buffer"), h.lastIndexOf("buffer", 15), h.lastIndexOf(Buffer.from("a")), h.lastIndexOf(97, 5), h.lastIndexOf("is", -20), h.lastIndexOf(""));
console.log(h.includes("a b"), h.includes("buffer", 21), h.includes(Buffer.from("fer,")), h.includes(32), h.includes(255));
console.log(h.indexOf("6275", 0, "hex"), h.indexOf("YnVm", 0, "base64"), h.indexOf(97 + 256), h.indexOf("is", 3));

// views
const base = Buffer.from("abcdef");
const s1 = base.slice(1, 3), s2 = base.subarray(-3), s3 = base.subarray(4, 2);
s1[0] = 66;
console.log(base.toString(), s1, s2, s3, base.slice(), base.slice(-2, -1).toString());

// swap
console.log(Buffer.from([1, 2, 3, 4]).swap16(), Buffer.from([1, 2, 3, 4, 5, 6, 7, 8]).swap32(), Buffer.from([1, 2, 3, 4, 5, 6, 7, 8]).swap64());
for (const fn of ["16", "32", "64"]) {
  try {
    const x = Buffer.from([1, 2, 3]);
    if (fn === "16") x.swap16(); else if (fn === "32") x.swap32(); else x.swap64();
  } catch (e: any) {
    console.log(e.name, e.code, e.message);
  }
}

// JSON and iteration
console.log(Buffer.from("hi").toJSON(), JSON.stringify(Buffer.from([1, 2])), JSON.stringify({ b: Buffer.from("A") }));
const it = Buffer.from("xyz");
for (const v of it) console.log(v);
for (const [i, v] of it.entries()) console.log(i, v);
console.log([...it.keys()], [...it.values()], Array.from(it));
let total = 0;
for (let i = 0; i < it.length; i++) total += it[i];
console.log(total);

// write
const wb = Buffer.alloc(10);
console.log(wb.write("héllo"), wb, wb.write("abc", 8), wb, wb.write("zz", 1, 1), wb.write("ffee", 2, "hex"), wb.write("6162", 4, 1, "hex"), wb);
console.log(wb.write("é", 9), wb, wb.write("AQID", "base64"), wb);
console.log(Buffer.alloc(3).write("abcdef"), Buffer.alloc(4).write("ab", 4));

// printing
console.log(Buffer.alloc(51, 1));
console.log(Buffer.alloc(60));
console.log([Buffer.from("a")], { k: Buffer.from("b") });
console.log(String(Buffer.from("text")), `${Buffer.from("tpl")}`);
console.log(Buffer.poolSize);
const nums: number[] = [1.7, 258];
console.log(Buffer.of(1, 2, 300), Buffer.from(nums), Buffer.byteLength(Buffer.from("abc")), Buffer.allocUnsafeSlow(2).length);
const hello = Buffer.from("hello");
console.log(hello.includes("ll", 1), hello.includes(Buffer.from("lo")), hello.indexOf(108 + 256), hello.lastIndexOf("l"), hello.includes(104, 1));
