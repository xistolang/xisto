// node:util/types as a module
import * as types from "node:util/types";
import { isDate, isPromise, isRegExp } from "node:util/types";
import * as util from "node:util";

const p: Promise<int> = Promise.resolve(1);
const values: any[] = [new Date(0), /x/g, p, new Map<string, int>(), new Set<int>(), new Error("e"), new TypeError("t"),
  new Uint8Array(2), new Int32Array(2), new Float64Array(2), Buffer.from("ab"), "s", 1, true, null, [1, 2], { a: 1 }];
for (const v of values) {
  console.log(types.isDate(v), types.isRegExp(v), types.isPromise(v), types.isMap(v), types.isSet(v), types.isNativeError(v),
    types.isUint8Array(v), types.isInt32Array(v), types.isFloat64Array(v), types.isTypedArray(v), types.isArrayBufferView(v),
    types.isBoxedPrimitive(v), types.isProxy(v), types.isDataView(v), types.isFloat32Array(v));
}
console.log(isDate(new Date()), isPromise(p), isRegExp("x"), util.types.isDate(new Date()) === isDate(new Date()));
console.log(types.isAnyArrayBuffer(new SharedArrayBuffer(8)), types.isSharedArrayBuffer(new SharedArrayBuffer(8)), types.isArrayBuffer(1));
