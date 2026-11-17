// xisto:util/types (node:util/types): util.types' checks as module functions, written in xisto over runtime/node/util.ts.
// What xisto has no value for is always false: boxed primitives (new Number(1)), Proxy, the arguments object,
// ArrayBuffer and DataView, the typed arrays other than Uint8Array, Int32Array and Float64Array, generator objects
// and iterators of a Map or Set as values, external values, module namespace objects, KeyObject and CryptoKey.
// isAsyncFunction and isGeneratorFunction are false too: a function value does not record how it was declared.

import { types } from "xisto:util";

export function isDate(v: any): boolean {
  return types.isDate(v);
}
export function isRegExp(v: any): boolean {
  return types.isRegExp(v);
}
export function isMap(v: any): boolean {
  return types.isMap(v);
}
export function isSet(v: any): boolean {
  return types.isSet(v);
}
export function isNativeError(v: any): boolean {
  return types.isNativeError(v);
}
export function isPromise(v: any): boolean {
  return types.isPromise(v);
}
export function isUint8Array(v: any): boolean {
  return types.isUint8Array(v);
}
export function isInt32Array(v: any): boolean {
  return types.isInt32Array(v);
}
export function isFloat64Array(v: any): boolean {
  return types.isFloat64Array(v);
}
export function isTypedArray(v: any): boolean {
  return types.isTypedArray(v);
}
export function isArrayBufferView(v: any): boolean {
  return types.isArrayBufferView(v);
}
export function isSharedArrayBuffer(v: any): boolean {
  return types.isSharedArrayBuffer(v);
}
export function isAnyArrayBuffer(v: any): boolean {
  return types.isAnyArrayBuffer(v);
}
export function isArrayBuffer(v: any): boolean {
  return types.isArrayBuffer(v);
}
export function isBoxedPrimitive(v: any): boolean {
  return types.isBoxedPrimitive(v);
}
export function isNumberObject(v: any): boolean {
  return types.isNumberObject(v);
}
export function isStringObject(v: any): boolean {
  return types.isStringObject(v);
}
export function isBooleanObject(v: any): boolean {
  return types.isBooleanObject(v);
}
export function isBigIntObject(v: any): boolean {
  return types.isBigIntObject(v);
}
export function isSymbolObject(v: any): boolean {
  return types.isSymbolObject(v);
}
export function isProxy(v: any): boolean {
  return types.isProxy(v);
}
export function isWeakMap(v: any): boolean {
  return types.isWeakMap(v);
}
export function isWeakSet(v: any): boolean {
  return types.isWeakSet(v);
}
export function isArgumentsObject(v: any): boolean {
  return false;
}
export function isAsyncFunction(v: any): boolean {
  return false;
}
export function isGeneratorFunction(v: any): boolean {
  return false;
}
export function isGeneratorObject(v: any): boolean {
  return false;
}
export function isMapIterator(v: any): boolean {
  return false;
}
export function isSetIterator(v: any): boolean {
  return false;
}
export function isDataView(v: any): boolean {
  return false;
}
export function isExternal(v: any): boolean {
  return false;
}
export function isModuleNamespaceObject(v: any): boolean {
  return false;
}
export function isUint8ClampedArray(v: any): boolean {
  return false;
}
export function isUint16Array(v: any): boolean {
  return false;
}
export function isUint32Array(v: any): boolean {
  return false;
}
export function isInt8Array(v: any): boolean {
  return false;
}
export function isInt16Array(v: any): boolean {
  return false;
}
export function isFloat16Array(v: any): boolean {
  return false;
}
export function isFloat32Array(v: any): boolean {
  return false;
}
export function isBigInt64Array(v: any): boolean {
  return false;
}
export function isBigUint64Array(v: any): boolean {
  return false;
}
export function isKeyObject(v: any): boolean {
  return false;
}
export function isCryptoKey(v: any): boolean {
  return false;
}
