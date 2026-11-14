// console.log/util.inspect of strings: Node's quotes and escapes, long strings split by UTF-16 length (emoji)
import util from "node:util";
console.log({ a: "x\x7fy\u0085z", b: "it's", c: "it's \"q\"", d: "it's \"q\" `b`", e: "\x0b\x00" });
console.log({ s: "😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀\nabc" });
console.log({ s: "a".repeat(40) + "\n" + "b".repeat(30) });
console.log(util.inspect({ a: { b: { c: { d: { e: 1 } } } } }, { depth: Infinity }));
console.log(util.inspect({ a: [1, 2, 3], b: "x" }, { breakLength: Infinity }));
console.log(util.inspect({ a: [1, 2, 3], b: "x" }, { compact: false }));
console.log(util.inspect("line1\nline2 is long enough to be split here ok", { breakLength: 20 }));
console.log(util.inspect([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27], { breakLength: 40 }));
console.log(util.isDeepStrictEqual([[1], [2, 3]], [[1], [2, 3]]), util.isDeepStrictEqual({ a: 1 }, { a: 1, b: undefined }), util.isDeepStrictEqual(new Map<number, number[]>([[1, [1]]]), new Map<number, number[]>([[1, [1]]])), util.isDeepStrictEqual(NaN, NaN), util.isDeepStrictEqual(0, -0));
