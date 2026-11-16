// xisto:assert (node:assert), written in xisto: ok (the module called as a function), equal, notEqual, strictEqual,
// notStrictEqual, deepEqual, notDeepEqual, deepStrictEqual, notDeepStrictEqual, throws, doesNotThrow, rejects,
// doesNotReject, match, doesNotMatch, fail, ifError and AssertionError, with Node's messages (the simple form
// "1 !== 2", the stacked one with "+ actual - expected" and the line diff of objects).
// node:assert/strict is runtime/node/assert_strict.ts: the strict functions under the loose names.
// The compiler reads assert(x) of the module as assert.ok(x), adds the source text of the call for ok's message
// (src/parser.c), and picks the form of throws/rejects/doesNotThrow/doesNotReject by their second argument (a class,
// a RegExp, a validation function, a message or an object to compare: src/checker.c, assert_func_overload).
// Differences from Node (docs/docs/guides/node-compatibility.md): the messages inspect objects with their keys in
// declaration order (Node sorts them); undefined reads as null; an object given to throws compares its values
// with deepStrictEqual (a RegExp value in it is not tested against the string); deepEqual compares primitives
// loosely and objects by their own keys (no prototype checks).

import { inspect } from "xisto:util";

declare function util_inspect(v: any, depth: int, breakLength: int, compact: int): string;
declare function util_kind(v: any): int;
declare function util_keys(v: any): string[];
declare function util_deep_equal(a: any, b: any): boolean;

const K_NULL: int = 0;
const K_STRING: int = 1;
const K_NUMBER: int = 2;
const K_BOOLEAN: int = 3;
const K_ARRAY: int = 4;
const K_ERROR: int = 9;
const K_FUNCTION: int = 10;
const K_SYMBOL: int = 11;
const K_BIGINT: int = 12;
const K_OBJECT: int = 18;
const K_SHAPE: int = 19;
const K_RECORD: int = 20;

/** The options of new AssertionError({ ... }). */
export interface AssertionErrorOptions {
  message?: string;
  actual?: any;
  expected?: any;
  operator?: string;
}

function isPrimitive(v: any): boolean {
  const k = util_kind(v);
  return k === K_NULL || k === K_STRING || k === K_NUMBER || k === K_BOOLEAN || k === K_SYMBOL || k === K_BIGINT;
}
function isObject(v: any): boolean {
  return !isPrimitive(v) && util_kind(v) !== K_FUNCTION;
}

// ---- inspectValue: Node's layout for the messages (compact: false, every level) ----

function isIdent(k: string): boolean {
  if (k.length === 0) return false;
  for (let i: int = 0; i < k.length; i++) {
    const c = k.charCodeAt(i);
    const letter = (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c === 95 || c === 36;
    if (!letter && !(i > 0 && c >= 48 && c <= 57)) return false;
  }
  return true;
}
function reindent(s: string, ind: string): string {
  return ind.length === 0 ? s : s.split("\n").join("\n" + ind);
}
function member(v: any, key: string): any {
  if (util_kind(v) === K_ARRAY) return v[Math.floor(Number(key)) as int];
  return v[key];
}
function iv(v: any, ind: string): string {
  const k = util_kind(v);
  if (k === K_ARRAY) {
    const keys = util_keys(v);
    if (keys.length === 0) return "[]";
    const lines: string[] = [];
    for (const key of keys) lines.push(ind + "  " + iv(member(v, key), ind + "  "));
    return "[\n" + lines.join(",\n") + "\n" + ind + "]";
  }
  if (k === K_OBJECT || k === K_SHAPE || k === K_RECORD) {
    const keys = util_keys(v);
    let prefix = "";
    if (k === K_OBJECT) {
      const s = util_inspect(v, -1, 80, 3); // "[Name]"
      if (s.length > 2 && s !== "[Object]") prefix = s.slice(1, s.length - 1) + " ";
    }
    if (keys.length === 0) return prefix + "{}";
    const lines: string[] = [];
    for (const key of keys) lines.push(ind + "  " + (isIdent(key) ? key : util_inspect(key, 0, 80, 0)) + ": " + iv(v[key], ind + "  "));
    return prefix + "{\n" + lines.join(",\n") + "\n" + ind + "}";
  }
  return reindent(util_inspect(v, 1000, 80, 0), ind);
}
function inspectValue(v: any): string {
  return iv(v, "");
}

// ---- the messages (Node's lib/internal/assert/assertion_error.js) ----

function readable(op: string): string {
  if (op === "deepStrictEqual") return "Expected values to be strictly deep-equal:";
  if (op === "strictEqual") return "Expected values to be strictly equal:";
  if (op === "strictEqualObject") return 'Expected "actual" to be reference-equal to "expected":';
  if (op === "deepEqual") return "Expected values to be loosely deep-equal:";
  if (op === "notDeepStrictEqual") return 'Expected "actual" not to be strictly deep-equal to:';
  if (op === "notStrictEqual") return 'Expected "actual" to be strictly unequal to:';
  if (op === "notStrictEqualObject") return 'Expected "actual" not to be reference-equal to "expected":';
  if (op === "notDeepEqual") return 'Expected "actual" not to be loosely deep-equal to:';
  if (op === "notIdentical") return "Values have same structure but are not reference-equal:";
  return "";
}

class DiffOp {
  type: string;
  value: string;
  constructor(type: string, value: string) {
    this.type = type;
    this.value = value;
  }
}

function linesEqual(a: string, b: string, comma: boolean): boolean {
  if (a === b) return true;
  return comma && (a + "," === b || a === b + ",");
}

// Myers' diff of the lines, as Node's lib/internal/assert/myers_diff.js (the result from the last line to the first)
function myersDiff(actual: string[], expected: string[], comma: boolean): DiffOp[] {
  const n = actual.length;
  const m = expected.length;
  const max = n + m;
  let v: int[] = [];
  for (let i: int = 0; i < 2 * max + 1; i++) v.push(0);
  const trace: int[][] = [];
  for (let d: int = 0; d <= max; d++) {
    trace.push(v.slice());
    for (let k: int = -d; k <= d; k += 2) {
      const off = k + max;
      let x: int;
      if (k === -d || (k !== d && v[off - 1] < v[off + 1])) x = v[off + 1];
      else x = v[off - 1] + 1;
      let y = x - k;
      while (x < n && y < m && linesEqual(actual[x], expected[y], comma)) {
        x++;
        y++;
      }
      v[off] = x;
      if (x >= n && y >= m) return backtrack(trace, actual, expected, comma);
    }
  }
  return [];
}
function backtrack(trace: int[][], actual: string[], expected: string[], comma: boolean): DiffOp[] {
  const max = actual.length + expected.length;
  let x: int = actual.length;
  let y: int = expected.length;
  const result: DiffOp[] = [];
  for (let d: int = trace.length - 1; d >= 0; d--) {
    const v = trace[d];
    const k = x - y;
    const off = k + max;
    let pk: int;
    if (k === -d || (k !== d && v[off - 1] < v[off + 1])) pk = k + 1;
    else pk = k - 1;
    const px = v[pk + max];
    const py = px - pk;
    while (x > px && y > py) {
      const a = actual[x - 1];
      result.push(new DiffOp("nop", comma && !a.endsWith(",") ? expected[y - 1] : a));
      x--;
      y--;
    }
    if (d > 0) {
      if (x > px) {
        result.push(new DiffOp("insert", actual[x - 1]));
        x--;
      } else {
        result.push(new DiffOp("delete", expected[y - 1]));
        y--;
      }
    }
  }
  return result;
}
const NOP_LINES: int = 5;
class Printed {
  message: string;
  skipped: boolean;
  constructor(message: string, skipped: boolean) {
    this.message = message;
    this.skipped = skipped;
  }
}
function printMyersDiff(diff: DiffOp[]): Printed {
  let message = "";
  let skipped = false;
  let nops: int = 0;
  const n = diff.length;
  for (let i: int = n - 1; i >= 0; i--) {
    const op = diff[i];
    const prev = i < n - 1 ? diff[i + 1].type : "";
    if (prev === "nop" && op.type !== prev) {
      if (nops === NOP_LINES + 1) message += "  " + diff[i + 1].value + "\n";
      else if (nops === NOP_LINES + 2) {
        message += "  " + diff[i + 2].value + "\n";
        message += "  " + diff[i + 1].value + "\n";
      } else if (nops >= NOP_LINES + 3) {
        message += "...\n";
        message += "  " + diff[i + 1].value + "\n";
        skipped = true;
      }
      nops = 0;
    }
    if (op.type === "insert") message += "+ " + op.value + "\n";
    else if (op.type === "delete") message += "- " + op.value + "\n";
    else if (nops < NOP_LINES) {
      message += "  " + op.value + "\n";
      nops++;
    } else nops++;
  }
  return new Printed("\n" + message.trimEnd(), skipped);
}

function errDiff(actual: any, expected: any, operator: string, custom: string | null): string {
  let op = operator;
  if (op === "strictEqual" && ((isObject(actual) && isObject(expected)) || (util_kind(actual) === K_FUNCTION && util_kind(expected) === K_FUNCTION)))
    op = "strictEqualObject";
  const ia = inspectValue(actual);
  const ie = inspectValue(expected);
  const la = ia.split("\n");
  const le = ie.split("\n");
  if (ia === ie && !isPrimitive(actual)) {
    // (the same text: equal in structure, not the same object)
    return (custom !== null ? custom : readable("notIdentical")) + "\n\n" + la.join("\n") + "\n";
  }
  let header = "+ actual - expected";
  let message = "";
  let skipped = false;
  const simple = la.length <= 1 && le.length <= 1 && (!isObject(actual) || !isObject(expected));
  if (simple) {
    let len = la[0].length + le[0].length;
    if (util_kind(actual) === K_STRING) len -= 2;
    if (util_kind(expected) === K_STRING) len -= 2;
    const bothZero = util_kind(actual) === K_NUMBER && util_kind(expected) === K_NUMBER && (actual as number) === 0 && (expected as number) === 0;
    if (len <= 12 && !bothZero) {
      message = `${la[0]} !== ${le[0]}`;
      header = "";
    } else {
      const a = la[0];
      const e = le[0];
      message = `\n+ ${a}\n- ${e}`;
      const strings = util_kind(actual) === K_STRING && util_kind(expected) === K_STRING;
      if (strings && a.length + e.length <= 80) {
        let idx: int = -1;
        for (let i: int = 0; i < a.length; i++) {
          if (i >= e.length || a.charCodeAt(i) !== e.charCodeAt(i)) {
            if (i >= 3) idx = i;
            break;
          }
        }
        if (idx !== -1) message += "\n" + " ".repeat(idx + 2) + "^";
      }
    }
  } else {
    const printed = printMyersDiff(myersDiff(la, le, isObject(actual)));
    message = printed.message;
    skipped = printed.skipped;
  }
  const head = (custom !== null ? custom : readable(op)) + "\n" + header;
  return head + (skipped ? "\n... Skipped lines" : "") + "\n" + message + "\n";
}

function generated(actual: any, expected: any, operator: string): string {
  if (operator === "deepStrictEqual" || operator === "strictEqual") return errDiff(actual, expected, operator, null);
  if (operator === "notDeepStrictEqual" || operator === "notStrictEqual") {
    let base = readable(operator);
    const res = inspectValue(actual).split("\n");
    if (operator === "notStrictEqual" && isObject(actual)) base = readable("notStrictEqualObject");
    if (res.length > 50) {
      res[46] = "...";
      while (res.length > 47) res.pop();
    }
    if (res.length === 1) return base + (res[0].length > 5 ? "\n\n" : " ") + res[0];
    return base + "\n\n" + res.join("\n") + "\n";
  }
  let res = inspectValue(actual);
  let other = inspectValue(expected);
  const known = readable(operator);
  if (operator === "notDeepEqual" && res === other) {
    res = `${known}\n\n${res}`;
    if (res.length > 1024) res = res.slice(0, 1021) + "...";
    return res;
  }
  if (res.length > 512) res = res.slice(0, 509) + "...";
  if (other.length > 512) other = other.slice(0, 509) + "...";
  if (operator === "deepEqual") res = `${known}\n\n${res}\n\nshould loosely deep-equal\n\n`;
  else if (operator === "notDeepEqual") res = `Expected values not to be loosely deep-equal:\n\n${res}\n\nshould not loosely deep-equal\n\n`;
  else other = ` ${operator} ${other}`;
  return res + other;
}

/** The error every failed assertion throws: code ERR_ASSERTION, with the values compared and the operator. */
export class AssertionError extends Error {
  actual: any;
  expected: any;
  operator: string;
  code: string = "ERR_ASSERTION";
  generatedMessage: boolean;
  constructor(options: AssertionErrorOptions) {
    super(messageOf(options));
    this.name = "AssertionError";
    this.actual = options.actual;
    this.expected = options.expected;
    this.operator = options.operator ?? "";
    this.generatedMessage = options.message === undefined || options.message === null;
  }
}
function messageOf(o: AssertionErrorOptions): string {
  const op = o.operator ?? "";
  if (o.message !== undefined && o.message !== null) {
    if (op === "deepStrictEqual" || op === "strictEqual") return errDiff(o.actual, o.expected, op, o.message);
    return o.message;
  }
  return generated(o.actual, o.expected, op);
}

function fails(actual: any, expected: any, operator: string, message: string | Error | null, generatedText: string | null = null): never {
  if (message instanceof Error) throw message;
  const custom = message as string | null;
  if (custom === null && generatedText !== null) {
    const e = new AssertionError({ message: generatedText, actual: actual, expected: expected, operator: operator });
    e.generatedMessage = true;
    throw e;
  }
  throw new AssertionError({ message: custom ?? undefined, actual: actual, expected: expected, operator: operator });
}

// ---- equality ----

function same(a: any, b: any): boolean {
  // Object.is
  if (util_kind(a) === K_NUMBER && util_kind(b) === K_NUMBER) {
    const x = a as number;
    const y = b as number;
    if (x !== x) return y !== y;
    if (x === 0 && y === 0) return 1 / x === 1 / y;
    return x === y;
  }
  return a === b;
}
function toNum(v: any): number {
  const k = util_kind(v);
  if (k === K_NUMBER) return v as number;
  if (k === K_BOOLEAN) return (v as boolean) ? 1 : 0;
  if (k === K_STRING) return Number(v as string);
  if (k === K_NULL) return 0;
  return NaN;
}
function loose(a: any, b: any): boolean {
  // ==, with NaN == NaN (as assert.equal reads it)
  const ka = util_kind(a);
  const kb = util_kind(b);
  if (ka === K_NULL || kb === K_NULL) return ka === kb;
  if (ka === K_NUMBER && kb === K_NUMBER && (a as number) !== (a as number) && (b as number) !== (b as number)) return true;
  if (isPrimitive(a) && isPrimitive(b)) {
    if (ka === kb) return a === b;
    return toNum(a) === toNum(b);
  }
  return a === b;
}
function looseDeep(a: any, b: any): boolean {
  if (isPrimitive(a) || isPrimitive(b)) return loose(a, b);
  const ka = util_kind(a);
  const kb = util_kind(b);
  const plain = (k: int): boolean => k === K_ARRAY || k === K_OBJECT || k === K_SHAPE || k === K_RECORD;
  if (!plain(ka) || !plain(kb)) return util_deep_equal(a, b);
  if ((ka === K_ARRAY) !== (kb === K_ARRAY)) return false;
  const keysA = util_keys(a);
  const keysB = util_keys(b);
  if (keysA.length !== keysB.length) return false;
  for (const k of keysA) {
    if (!keysB.includes(k)) return false;
    if (!looseDeep(member(a, k), member(b, k))) return false;
  }
  return true;
}

// ---- the assertions ----

/** Fails when value is falsy (src is the call as written, added by the compiler for the message). */
export function ok(value: any, message: string | Error | null = null, src: string = ""): void {
  if (value) return;
  if (message === null) {
    const text = src.length > 0 ? `The expression evaluated to a falsy value:\n\n  ${src}\n` : "No value argument passed to `assert.ok()`";
    fails(value, true, "==", null, text);
  }
  fails(value, true, "==", message);
}

/** actual == expected (NaN equals NaN). */
export function equal(actual: any, expected: any, message: string | Error | null = null): void {
  if (!loose(actual, expected)) fails(actual, expected, "==", message);
}
/** actual != expected. */
export function notEqual(actual: any, expected: any, message: string | Error | null = null): void {
  if (loose(actual, expected)) fails(actual, expected, "!=", message);
}
/** Object.is(actual, expected). */
export function strictEqual(actual: any, expected: any, message: string | Error | null = null): void {
  if (!same(actual, expected)) fails(actual, expected, "strictEqual", message);
}
/** !Object.is(actual, expected). */
export function notStrictEqual(actual: any, expected: any, message: string | Error | null = null): void {
  if (same(actual, expected)) fails(actual, expected, "notStrictEqual", message);
}
/** The same structure, primitives compared with ==. */
export function deepEqual(actual: any, expected: any, message: string | Error | null = null): void {
  if (!looseDeep(actual, expected)) fails(actual, expected, "deepEqual", message);
}
export function notDeepEqual(actual: any, expected: any, message: string | Error | null = null): void {
  if (looseDeep(actual, expected)) fails(actual, expected, "notDeepEqual", message);
}
/** The same structure and values (util.isDeepStrictEqual). */
export function deepStrictEqual(actual: any, expected: any, message: string | Error | null = null): void {
  if (!util_deep_equal(actual, expected)) fails(actual, expected, "deepStrictEqual", message);
}
export function notDeepStrictEqual(actual: any, expected: any, message: string | Error | null = null): void {
  if (util_deep_equal(actual, expected)) fails(actual, expected, "notDeepStrictEqual", message);
}
/** Fails with the message (or "Failed"); an Error is thrown as it is. */
export function fail(message: string | Error | null = null): never {
  if (message instanceof Error) throw message;
  if (message === null) fails(null, null, "fail", null, "Failed");
  throw new AssertionError({ message: message as string, operator: "fail" });
}
/** Throws value when it is neither null nor undefined. */
export function ifError(value: any): void {
  if (value === null || value === undefined) return;
  let m: string;
  const k = util_kind(value);
  if (k === K_ERROR || (isObject(value) && util_kind(value.message) === K_STRING)) m = value.message as string;
  else m = inspect(value);
  const e = new AssertionError({ message: `ifError got unwanted exception: ${m}`, actual: value, expected: null, operator: "ifError" });
  e.generatedMessage = false;
  throw e;
}
/** The string matches the regular expression. */
export function match(value: string, re: RegExp, message: string | Error | null = null): void {
  if (re.test(value)) return;
  fails(value, re, "match", message, `The input did not match the regular expression ${re.toString()}. Input:\n\n${util_inspect(value, 0, 80, 3)}\n`);
}
export function doesNotMatch(value: string, re: RegExp, message: string | Error | null = null): void {
  if (!re.test(value)) return;
  fails(value, re, "doesNotMatch", message, `The input was expected to not match the regular expression ${re.toString()}. Input:\n\n${util_inspect(value, 0, 80, 3)}\n`);
}

// throws & co: the error the function threw, or null when it did not throw (thrown: whether it did)
class Caught {
  thrown: boolean = false;
  error: any = null;
}
function run(fn: () => void): Caught {
  const c = new Caught();
  try {
    fn();
  } catch (e: any) {
    c.thrown = true;
    c.error = e;
  }
  return c;
}
async function runAsync(fn: () => Promise<any>): Promise<Caught> {
  const c = new Caught();
  try {
    await fn();
  } catch (e: any) {
    c.thrown = true;
    c.error = e;
  }
  return c;
}
function missing(kind: string, name: string, message: string | null): never {
  let details = name.length > 0 ? ` (${name})` : "";
  details += message !== null ? `: ${message}` : ".";
  const e = new AssertionError({ message: `Missing expected ${kind}${details}`, operator: kind === "exception" ? "throws" : "rejects" });
  e.generatedMessage = false;
  throw e;
}
// String(err): "Name: message" for an Error (or an object with a message), String(v) for anything else
function errText(err: any): string {
  if (util_kind(err) === K_ERROR || (isObject(err) && util_kind(err.message) === K_STRING)) {
    const m = err.message as string;
    const nm = util_kind(err.name) === K_STRING ? (err.name as string) : "Error";
    return m.length > 0 ? `${nm}: ${m}` : nm;
  }
  return String(err);
}
function checkObject(err: any, expected: any, operator: string, message: string | null): void {
  if (expected === null) return;
  const keys = util_keys(expected);
  let okAll = true;
  for (const k of keys) if (!util_deep_equal(err[k], expected[k])) okAll = false;
  if (okAll) return;
  // Node's Comparison objects: the keys compared, sorted, from both sides
  const sorted = keys.slice().sort();
  const la: string[] = ["Comparison {"];
  const le: string[] = ["Comparison {"];
  for (let i: int = 0; i < sorted.length; i++) {
    const k = sorted[i];
    const sep = i < sorted.length - 1 ? "," : "";
    la.push("  " + k + ": " + reindent(inspectValue(err[k]), "  ") + sep);
    le.push("  " + k + ": " + reindent(inspectValue(expected[k]), "  ") + sep);
  }
  la.push("}");
  le.push("}");
  const printed = printMyersDiff(myersDiff(la.join("\n").split("\n"), le.join("\n").split("\n"), true));
  const head = (message !== null ? message : readable("deepStrictEqual")) + "\n+ actual - expected";
  fails(err, expected, operator, null, head + (printed.skipped ? "\n... Skipped lines" : "") + "\n" + printed.message + "\n");
}
function checkRe(err: any, re: RegExp, operator: string, message: string | null): void {
  const s = errText(err);
  if (re.test(s)) return;
  fails(err, re, operator, message, `The input did not match the regular expression ${re.toString()}. Input:\n\n${util_inspect(s, 0, 80, 3)}\n`);
}
function checkFn(err: any, validate: (e: any) => boolean, operator: string, message: string | null): void {
  if (validate(err)) return;
  fails(err, null, operator, message, `The validation function is expected to return "true". Received false\n\nCaught error:\n\n${errText(err)}`);
}
function checkClass(err: any, isa: (e: any) => boolean, name: string, operator: string, message: string | null): void {
  if (isa(err)) return;
  const got = util_kind(err) === K_ERROR || isObject(err) ? String(err.name) : inspect(err);
  let text = `The error is expected to be an instance of "${name}". Received "${got}"`;
  if (util_kind(err) === K_ERROR || (isObject(err) && util_kind(err.message) === K_STRING)) text += `\n\nError message:\n\n${err.message}`;
  fails(err, null, operator, message, text);
}

/** fn must throw; expected (an object of the properties to compare) checks the error. */
export function throws(fn: () => void, expected: any = null, message: string | null = null): void {
  const c = run(fn);
  if (!c.thrown) missing("exception", "", message);
  checkObject(c.error, expected, "throws", message);
}
export function __throwsMsg(fn: () => void, message: string): void {
  if (!run(fn).thrown) missing("exception", "", message);
}
export function __throwsRe(fn: () => void, re: RegExp, message: string | null = null): void {
  const c = run(fn);
  if (!c.thrown) missing("exception", "", message);
  checkRe(c.error, re, "throws", message);
}
export function __throwsFn(fn: () => void, validate: (e: any) => boolean, message: string | null = null): void {
  const c = run(fn);
  if (!c.thrown) missing("exception", "", message);
  checkFn(c.error, validate, "throws", message);
}
export function __throwsClass(fn: () => void, isa: (e: any) => boolean, name: string, message: string | null = null): void {
  const c = run(fn);
  if (!c.thrown) missing("exception", name, message);
  checkClass(c.error, isa, name, "throws", message);
}
function unwanted(kind: string, err: any, message: string | null): never {
  const e = new AssertionError({
    message: `Got unwanted ${kind}${message !== null ? ": " + message : "."}\nActual message: "${err.message}"`,
    actual: err,
    operator: kind === "exception" ? "doesNotThrow" : "doesNotReject",
  });
  e.generatedMessage = false;
  throw e;
}
/** fn must not throw (a validation function before the message is accepted and not used). */
export function doesNotThrow(fn: () => void, message: string | null = null): void {
  const c = run(fn);
  if (c.thrown) unwanted("exception", c.error, message);
}
/** fn must not throw an error of the class (any other error is thrown again). */
export function __doesNotThrowClass(fn: () => void, isa: (e: any) => boolean, name: string, message: string | null = null): void {
  const c = run(fn);
  if (!c.thrown) return;
  if (!isa(c.error)) throw c.error;
  unwanted("exception", c.error, message);
}
/** fn must not throw an error whose text matches (any other error is thrown again). */
export function __doesNotThrowRe(fn: () => void, re: RegExp, message: string | null = null): void {
  const c = run(fn);
  if (!c.thrown) return;
  if (!re.test(errText(c.error))) throw c.error;
  unwanted("exception", c.error, message);
}

/** The promise fn returns must reject; expected (an object of the properties to compare) checks the error. */
export async function rejects(fn: () => Promise<any>, expected: any = null, message: string | null = null): Promise<void> {
  const c = await runAsync(fn);
  if (!c.thrown) missing("rejection", "", message);
  checkObject(c.error, expected, "rejects", message);
}
export async function __rejectsMsg(fn: () => Promise<any>, message: string): Promise<void> {
  const c = await runAsync(fn);
  if (!c.thrown) missing("rejection", "", message);
}
export async function __rejectsRe(fn: () => Promise<any>, re: RegExp, message: string | null = null): Promise<void> {
  const c = await runAsync(fn);
  if (!c.thrown) missing("rejection", "", message);
  checkRe(c.error, re, "rejects", message);
}
export async function __rejectsFn(fn: () => Promise<any>, validate: (e: any) => boolean, message: string | null = null): Promise<void> {
  const c = await runAsync(fn);
  if (!c.thrown) missing("rejection", "", message);
  checkFn(c.error, validate, "rejects", message);
}
export async function __rejectsClass(fn: () => Promise<any>, isa: (e: any) => boolean, name: string, message: string | null = null): Promise<void> {
  const c = await runAsync(fn);
  if (!c.thrown) missing("rejection", name, message);
  checkClass(c.error, isa, name, "rejects", message);
}
/** The promise fn returns must not reject. */
export async function doesNotReject(fn: () => Promise<any>, message: string | null = null): Promise<void> {
  const c = await runAsync(fn);
  if (c.thrown) unwanted("rejection", c.error, message);
}
export async function __doesNotRejectClass(fn: () => Promise<any>, isa: (e: any) => boolean, name: string, message: string | null = null): Promise<void> {
  const c = await runAsync(fn);
  if (!c.thrown) return;
  if (!isa(c.error)) throw c.error;
  unwanted("rejection", c.error, message);
}
export async function __doesNotRejectRe(fn: () => Promise<any>, re: RegExp, message: string | null = null): Promise<void> {
  const c = await runAsync(fn);
  if (!c.thrown) return;
  if (!re.test(errText(c.error))) throw c.error;
  unwanted("rejection", c.error, message);
}
