// node:util: format and inspect (the same text as console.log), isDeepStrictEqual, types (same output as Node)
import util, { format, formatWithOptions, inspect, isDeepStrictEqual, types } from "node:util";
import { inspect as inspect2 } from "util";

class Point {
  x: number;
  y: number;
  constructor(x: number, y: number) {
    this.x = x;
    this.y = y;
  }
}
class Money {
  cents: int;
  constructor(cents: int) {
    this.cents = cents;
  }
  toString(): string {
    return `$${(this.cents / 100).toFixed(2)}`;
  }
}
interface User {
  name: string;
  age: int;
  tags: string[];
  address: { city: string; geo: { lat: number; lng: number } };
}

const user: User = { name: "Ana", age: 31, tags: ["admin", "ops"], address: { city: "Recife", geo: { lat: -8.05, lng: -34.9 } } };

// ---- format ----
console.log(format("%s is %d years old", "Ana", 31));
console.log(format("%s:%s", "a"));
console.log(format("%d%% done", 50));
console.log(format("100%"));
console.log(format("%% %s", "x"));
console.log(format("no placeholders", 1, "two", true));
console.log(format("%i | %i | %i", 42.9, "17px", -3.7));
console.log(format("%f | %f | %f", "3.25abc", 1.5, "x"));
console.log(format("%d | %d | %d | %d", "42", true, -0, "abc"));
console.log(format("%s | %s | %s", -0, 1.5, null));
console.log(format("%j", { a: [1, 2], b: "x" }));
console.log(format("%j and %j", [1, "two"], "str"));
console.log(format("%O", user));
console.log(format("%s", user));
console.log(format("%s", new Point(1, 2)));
console.log(format("total: %s", new Money(1234)));
const nestedArr: int[][][][] = [[[[1, 2]]], [[[3]]]];
console.log(format("%s", nestedArr));
console.log(format("%c styled %s", "color: red", "text"));
console.log(format("%x %s", "a"));
console.log(format(42, "a", { b: 1 }));
console.log(format("a", "b", "c"));
console.log(format(new Point(3, 4), [1, 2]));
console.log(format());
console.log(format("%s %s %s", "a", "b"));
console.log(format("%o", { list: { a: { b: { c: { d: { e: 1 } } } } } }));
console.log(formatWithOptions({ depth: 0 }, "%O and %s", user, "x"));
console.log(formatWithOptions({ breakLength: 200 }, "user:", user));
console.log(util.format("%s=%d", "n", 7));

// ---- inspect ----
console.log(inspect("text"));
console.log(inspect("it's"));
console.log(inspect(`it's "quoted"`));
console.log(inspect("tab\there\\back"));
console.log(inspect(42), inspect(-0), inspect(1e21), inspect(true), inspect(null));
console.log(inspect([1, 2, 3]));
console.log(inspect(user));
console.log(inspect(user, { depth: 0 }));
console.log(inspect(user, { depth: 1 }));
console.log(inspect(user, { depth: Infinity }));
console.log(inspect(user, { compact: false }));
console.log(inspect(user, { breakLength: Infinity }));
console.log(inspect(user, { breakLength: 40 }));
console.log(inspect(new Point(1.5, -2)));
console.log(inspect(new Map<string, int>([["a", 1], ["b", 2]])));
console.log(inspect(new Set<string>(["x", "y"])));
console.log(inspect(new Date(0)));
const nums: int[] = [];
for (let i = 0; i < 40; i++) nums.push(i * i);
console.log(inspect(nums));
console.log(inspect(nums, { compact: false }).split("\n").length);
console.log(inspect(nums, { breakLength: 120 }));
const many: int[] = [];
for (let i = 0; i < 120; i++) many.push(i);
console.log(inspect(many));
console.log(inspect({ note: "first line of a long note\nsecond line of the same long note\nthird line" }));
const nestedArr2: int[][][][][] = [[[[[1, 2]]]], [[[[3]]]]];
console.log(inspect(nestedArr2, { depth: 2 }));
console.log(inspect2({ a: { b: { c: { d: {} } } } }));
console.log(inspect({ s: "multi\nline" }));
console.log(inspect({ s: "multi\nline" }, { compact: 1 }));
console.log(inspect({ a: 1, b: { c: 2 } }, { compact: 1 }));
console.log(inspect(Buffer.from("hi")), inspect(new Uint8Array([1, 2])));

// ---- isDeepStrictEqual ----
const eq = (a: any, b: any): void => console.log(isDeepStrictEqual(a, b));
eq(1, 1);
eq(1, "1");
eq(NaN, NaN);
eq(0, -0);
eq("a", "a");
eq([1, 2, 3], [1, 2, 3]);
eq([1, 2, 3], [1, 2, 4]);
eq([1, 2], [1, 2, 3]);
eq(user, { name: "Ana", age: 31, tags: ["admin", "ops"], address: { city: "Recife", geo: { lat: -8.05, lng: -34.9 } } });
eq(user, { name: "Ana", age: 31, tags: ["admin", "ops"], address: { city: "Recife", geo: { lat: -8.05, lng: -34 } } });
eq(new Point(1, 2), new Point(1, 2));
eq(new Point(1, 2), new Point(2, 1));
eq(new Map<string, int>([["a", 1], ["b", 2]]), new Map<string, int>([["b", 2], ["a", 1]]));
eq(new Map<string, int>([["a", 1]]), new Map<string, int>([["a", 2]]));
eq(new Set<int>([1, 2, 3]), new Set<int>([3, 2, 1]));
eq(new Set<int>([1, 2]), new Set<int>([1, 3]));
eq(new Date(5), new Date(5));
eq(new Date(5), new Date(6));
eq([[1, 2], [3]], [[1, 2], [3]]);
eq(Buffer.from("abc"), Buffer.from("abc"));
eq(Buffer.from("abc"), Buffer.from("abd"));
eq(null, null);
eq(null, 0);
const r1: Record<string, int> = { a: 1, b: 2 };
const r2: Record<string, int> = { b: 2, a: 1 };
eq(r1, r2);
r2["c"] = 3;
eq(r1, r2);

// ---- types ----
const t = types;
console.log(t.isDate(new Date()), t.isDate("2020"), t.isRegExp(/a/), t.isRegExp("a"));
console.log(t.isMap(new Map<string, int>()), t.isSet(new Set<int>()), t.isMap(new Set<int>()));
console.log(t.isNativeError(new Error("x")), t.isNativeError(new TypeError("y")), t.isNativeError({ message: "z" }));
console.log(t.isUint8Array(new Uint8Array(2)), t.isUint8Array(Buffer.from("a")), t.isUint8Array([1]));
const pending: Promise<int> = Promise.resolve(1);
console.log(t.isTypedArray(new Float64Array(2)), t.isTypedArray([1, 2]), t.isPromise(pending), t.isPromise({}));
console.log(util.types.isDate(new Date(0)));
