// xisto:util (node:util): format, formatWithOptions, inspect, isDeepStrictEqual, promisify, callbackify, deprecate,
// debuglog, types, parseArgs, styleText, stripVTControlCharacters, toUSVString, getSystemErrorName, TextEncoder and
// TextDecoder. inspect() is console.log's own layout (runtime: xs_util_inspect), so the two always agree.
// Differences from Node (docs/docs/guides/node-compatibility.md):
//   - inspect options: depth, breakLength and compact (a number or false) are honoured; colors, showHidden, sorted,
//     maxArrayLength, maxStringLength and getters are accepted and ignored; depth: null is the default (2), not
//     unlimited (one empty value): pass Infinity; compact: true is read as 3;
//   - %o is %O with depth 4 (no hidden [length] entries); %s of a function prints [Function], not its source;
//   - promisify and callbackify take functions of one argument plus the callback (TypeScript cannot give a single
//     generic signature for every arity); deprecate and debuglog return (...args: any[]) => any functions;
//   - parseArgs values print as a plain object (Node: [Object: null prototype]).

declare function util_inspect(v: any, depth: int, breakLength: int, compact: int): string;
declare function util_kind(v: any): int;
declare function util_has_method(v: any, name: string): boolean;
declare function util_deep_equal(a: any, b: any): boolean;
declare function util_isatty(fd: int): boolean;
declare function util_errname(err: int): string;

// util_kind's codes (runtime/xisto_rt.h, xs_util_kind)
const K_NULL: int = 0;
const K_STRING: int = 1;
const K_NUMBER: int = 2;
const K_BOOLEAN: int = 3;
const K_MAP: int = 5;
const K_SET: int = 6;
const K_DATE: int = 7;
const K_ERROR: int = 9;
const K_FUNCTION: int = 10;
const K_SYMBOL: int = 11;
const K_BIGINT: int = 12;
const K_U8: int = 13;
const K_F64: int = 14;
const K_I32: int = 15;
const K_SAB: int = 16;

/** The options of inspect(): depth, breakLength and compact change the layout; the others are accepted. */
export interface InspectOptions {
  depth?: number;
  breakLength?: number;
  compact?: number | boolean;
  colors?: boolean;
  showHidden?: boolean;
  sorted?: boolean;
  maxArrayLength?: number;
  maxStringLength?: number;
  getters?: boolean;
  numericSeparator?: boolean;
  customInspect?: boolean;
  showProxy?: boolean;
}

const BIG: int = 1000000000;

function optInt(v: number, dflt: int): int {
  if (v !== v) return dflt;
  if (v > 1000000000) return BIG;
  if (v < -1000000000) return -BIG;
  return Math.floor(v) as int;
}

function inspectWith(value: any, depth: int, breakLength: int, compact: int): string {
  return util_inspect(value, depth, breakLength, compact);
}

function compactOf(c: any): int {
  if (c === null) return 3;
  if (typeof c === "boolean") return (c as boolean) ? 3 : 0;
  return optInt(c as number, 3);
}

/** The text console.log prints for the value (strings quoted, as inside an object). */
export function inspect(value: any, options: InspectOptions | null = null): string {
  if (options === null) return util_inspect(value, 2, 80, 3);
  const d = options.depth;
  const b = options.breakLength;
  const depth = d !== null && d !== undefined ? optInt(d, 2) : 2;
  const brk = b !== null && b !== undefined ? optInt(b, 80) : 80;
  return util_inspect(value, depth, brk, compactOf(options.compact));
}

function numText(n: number): string {
  if (n === 0 && 1 / n < 0) return "-0";
  return `${n}`;
}

function numberOf(v: any): number {
  const k = util_kind(v);
  if (k === K_NULL) return 0;
  if (k === K_NUMBER) return v as number;
  if (k === K_BOOLEAN) return (v as boolean) ? 1 : 0;
  if (k === K_STRING) return Number(v as string);
  if (k === K_DATE) return (v as Date).getTime();
  return NaN;
}

function stringOf(v: any): string {
  const k = util_kind(v);
  if (k === K_STRING) return v as string;
  if (k === K_NUMBER) return numText(v as number);
  return String(v);
}

function isObjectKind(k: int): boolean {
  return k !== K_NULL && k !== K_STRING && k !== K_NUMBER && k !== K_BOOLEAN && k !== K_SYMBOL && k !== K_BIGINT && k !== K_FUNCTION;
}

// %s: a number as written (-0 too), an object without a toString of its own as inspect at depth 0, the rest String()
function percentS(v: any, breakLength: int): string {
  const k = util_kind(v);
  if (k === K_NUMBER) return numText(v as number);
  if (k === K_BIGINT) return `${v}n`;
  if (k === K_FUNCTION) return util_inspect(v, 0, breakLength, 3);
  if (isObjectKind(k)) {
    if (util_has_method(v, "toString")) return v.toString() as string;
    return util_inspect(v, 0, breakLength, 3);
  }
  return String(v);
}

function jsonOf(v: any): string {
  if (util_kind(v) === K_FUNCTION) return "undefined";
  try {
    return JSON.stringify(v);
  } catch (e) {
    const msg = (e as Error).message;
    if (msg.indexOf("circular") >= 0 || msg.indexOf("Circular") >= 0) return "[Circular]";
    throw e;
  }
}

function formatArgs(depth: int, breakLength: int, compact: int, args: any[]): string {
  if (args.length === 0) return "";
  const first: any = args[0];
  let a: int = 0;
  let str = "";
  let join = "";
  if (util_kind(first) === K_STRING) {
    const f = first as string;
    if (args.length === 1) return f;
    let lastPos: int = 0;
    let i: int = 0;
    const n: int = f.length;
    while (i < n - 1) {
      if (f.charCodeAt(i) === 37) {
        i++;
        const c = f.charCodeAt(i);
        if (a + 1 !== args.length) {
          let tempStr = "";
          let take = true;
          if (c === 115) tempStr = percentS(args[++a], breakLength);
          else if (c === 106) tempStr = jsonOf(args[++a]);
          else if (c === 100) {
            const v: any = args[++a];
            const k = util_kind(v);
            tempStr = k === K_BIGINT ? `${v}n` : k === K_SYMBOL ? "NaN" : numText(numberOf(v));
          } else if (c === 79) tempStr = util_inspect(args[++a], depth, breakLength, compact);
          else if (c === 111) tempStr = util_inspect(args[++a], 4, breakLength, compact);
          else if (c === 105) {
            const v: any = args[++a];
            const k = util_kind(v);
            tempStr = k === K_BIGINT ? `${v}n` : k === K_SYMBOL ? "NaN" : numText(parseInt(stringOf(v)));
          } else if (c === 102) {
            const v: any = args[++a];
            tempStr = util_kind(v) === K_SYMBOL ? "NaN" : numText(parseFloat(stringOf(v)));
          } else if (c === 99) {
            a++;
            tempStr = "";
          } else if (c === 37) {
            str += f.slice(lastPos, i);
            lastPos = i + 1;
            take = false;
          } else take = false;
          if (take) {
            if (lastPos !== i - 1) str += f.slice(lastPos, i - 1);
            str += tempStr;
            lastPos = i + 1;
          }
        } else if (c === 37) {
          str += f.slice(lastPos, i);
          lastPos = i + 1;
        }
      }
      i++;
    }
    if (lastPos !== 0) {
      a++;
      join = " ";
      if (lastPos < n) str += f.slice(lastPos);
    }
  }
  while (a < args.length) {
    const v: any = args[a];
    str += join;
    str += util_kind(v) === K_STRING ? (v as string) : util_inspect(v, depth, breakLength, compact);
    join = " ";
    a++;
  }
  return str;
}

/** printf-like formatting as console.log does it: %s %d %i %f %j %o %O %c %%, the other arguments after a space. */
export function format(...args: any[]): string {
  return formatArgs(2, 80, 3, args);
}

/** format() with inspect options for the values it inspects. */
export function formatWithOptions(options: InspectOptions, ...args: any[]): string {
  const d = options.depth;
  const b = options.breakLength;
  return formatArgs(d !== null && d !== undefined ? optInt(d, 2) : 2, b !== null && b !== undefined ? optInt(b, 80) : 80, compactOf(options.compact), args);
}

/** Deep equality as assert.deepStrictEqual: primitives by Object.is, objects of the same class field by field,
 *  arrays element by element, Map and Set entries in any order. */
export function isDeepStrictEqual(a: any, b: any): boolean {
  return util_deep_equal(a, b);
}

/** The callback-taking function as one that returns a promise: (arg) => Promise<result>. */
export function promisify<A, R>(fn: (arg: A, callback: (err: Error | null, result: R) => void) => void): (arg: A) => Promise<R> {
  return (arg: A): Promise<R> =>
    new Promise<R>((resolve, reject) => {
      fn(arg, (err: Error | null, result: R) => {
        if (err !== null) reject(err);
        else resolve(result);
      });
    });
}

/** The promise-returning function as one that takes a callback (err, result) last. */
export function callbackify<A, R>(fn: (arg: A) => Promise<R>): (arg: A, callback: (err: Error | null, result: R | null) => void) => void {
  return (arg: A, callback: (err: Error | null, result: R | null) => void): void => {
    fn(arg).then(
      (r: R) => {
        callback(null, r);
      },
      (e: Error) => {
        callback(e, null);
      },
    );
  };
}

const warnedCodes: Map<string, boolean> = new Map<string, boolean>();
let warnedOnce = false;

// Node's process.emitWarning: printed after the current synchronous code (a microtask here; Node: nextTick), the
// --trace-deprecation hint with the first warning only
function emitDeprecation(msg: string, code: string | null): void {
  if (process.env["NODE_NO_WARNINGS"] === "1") return;
  const hint = !warnedOnce;
  warnedOnce = true;
  const text = `(node:${process.pid}) ${code !== null ? `[${code}] ` : ""}DeprecationWarning: ${msg}`;
  Promise.resolve(0).then((_: number) => {
    console.error(hint ? `${text}\n(Use \`node --trace-deprecation ...\` to show where the warning was created)` : text);
  });
}

/** The function, wrapped to print a DeprecationWarning to stderr the first time it is called (once per code). */
export function deprecate(fn: (...args: any[]) => any, msg: string, code: string | null = null): (...args: any[]) => any {
  let warned = false;
  return (...args: any[]): any => {
    if (!warned) {
      warned = true;
      const seen = code !== null && warnedCodes.has(code);
      if (code !== null) warnedCodes.set(code, true);
      if (!seen) emitDeprecation(msg, code);
    }
    return fn(...args);
  };
}

function debugEnabled(section: string): boolean {
  const env = process.env["NODE_DEBUG"];
  if (env === undefined || env === null || env === "") return false;
  const name = section.toUpperCase();
  for (const raw of (env as string).split(",")) {
    const pat = raw.trim().toUpperCase();
    if (pat === "") continue;
    if (pat.indexOf("*") < 0) {
      if (pat === name) return true;
      continue;
    }
    // a pattern with * wildcards: each * any run of characters
    const parts = pat.split("*");
    let pos: int = 0;
    let ok = name.startsWith(parts[0]);
    if (ok) pos = parts[0].length;
    for (let i = 1; i < parts.length && ok; i++) {
      const p = parts[i];
      if (i === parts.length - 1) {
        ok = p === "" || (name.length - p.length >= pos && name.endsWith(p));
      } else {
        const at = name.indexOf(p, pos);
        if (at < 0) ok = false;
        else pos = at + p.length;
      }
    }
    if (ok) return true;
  }
  return false;
}

/** A logger that writes "SECTION pid: message" to stderr when NODE_DEBUG names the section (commas, * wildcards). */
export function debuglog(section: string): (...args: any[]) => void {
  const on = debugEnabled(section);
  const name = section.toUpperCase();
  return (...args: any[]): void => {
    if (on) console.error(`${name} ${process.pid}: ${formatArgs(2, 80, 3, args)}`);
  };
}

/** The same as debuglog(). */
export function debug(section: string): (...args: any[]) => void {
  return debuglog(section);
}

/** Type checks on values of any kind (util.types). */
export class UtilTypes {
  isDate(v: any): boolean {
    return util_kind(v) === K_DATE;
  }
  isRegExp(v: any): boolean {
    return v instanceof RegExp;
  }
  isMap(v: any): boolean {
    return util_kind(v) === K_MAP;
  }
  isSet(v: any): boolean {
    return util_kind(v) === K_SET;
  }
  isNativeError(v: any): boolean {
    return util_kind(v) === K_ERROR || v instanceof Error;
  }
  isPromise(v: any): boolean {
    const k = util_kind(v);
    return k === 18 && util_inspect(v, 0, 80, 3).startsWith("Promise {");
  }
  isUint8Array(v: any): boolean {
    return util_kind(v) === K_U8;
  }
  isFloat64Array(v: any): boolean {
    return util_kind(v) === K_F64;
  }
  isInt32Array(v: any): boolean {
    return util_kind(v) === K_I32;
  }
  isTypedArray(v: any): boolean {
    const k = util_kind(v);
    return k === K_U8 || k === K_F64 || k === K_I32;
  }
  isArrayBufferView(v: any): boolean {
    return this.isTypedArray(v);
  }
  isArrayBuffer(v: any): boolean {
    return false;
  }
  isSharedArrayBuffer(v: any): boolean {
    return util_kind(v) === K_SAB;
  }
  isAnyArrayBuffer(v: any): boolean {
    return util_kind(v) === K_SAB;
  }
  isSymbolObject(v: any): boolean {
    return false;
  }
  isBoxedPrimitive(v: any): boolean {
    return false;
  }
  isNumberObject(v: any): boolean {
    return false;
  }
  isStringObject(v: any): boolean {
    return false;
  }
  isBooleanObject(v: any): boolean {
    return false;
  }
  isBigIntObject(v: any): boolean {
    return false;
  }
  isProxy(v: any): boolean {
    return false;
  }
  isWeakMap(v: any): boolean {
    return false;
  }
  isWeakSet(v: any): boolean {
    return false;
  }
}

/** util.types: isDate, isRegExp, isMap, isSet, isNativeError, isPromise, isUint8Array, isTypedArray... */
export const types: UtilTypes = new UtilTypes();

// ---- parseArgs ----

/** One option of parseArgs: type "string" or "boolean", an optional one-letter short name, multiple, default. */
export interface ParseArgsOptionConfig {
  type: string;
  short?: string;
  multiple?: boolean;
  default?: any;
}

/** parseArgs' configuration (args: process.argv after the script by default). */
export interface ParseArgsConfig {
  args?: string[];
  options?: Record<string, ParseArgsOptionConfig>;
  strict?: boolean;
  allowPositionals?: boolean;
  allowNegative?: boolean;
  tokens?: boolean;
}

/** A token of parseArgs (tokens: true): kind "option", "positional" or "option-terminator". */
export class ParseArgsToken {
  kind: string;
  index: int;
  name: string | null = null;
  rawName: string | null = null;
  value: string | null = null;
  inlineValue: boolean | null = null;
  constructor(kind: string, index: int) {
    this.kind = kind;
    this.index = index;
  }
}

/** What parseArgs gives: the options' values by long name, the positionals, and the tokens when asked. */
export class ParsedResults {
  values: Record<string, any> = {};
  positionals: string[] = [];
  tokens: ParseArgsToken[] | null = null;
}

/** The error parseArgs throws (a TypeError with Node's code: ERR_PARSE_ARGS_...). */
export class ParseArgsError extends TypeError {
  code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}

function optOf(options: Record<string, ParseArgsOptionConfig>, name: string): ParseArgsOptionConfig | null {
  return name in options ? options[name] : null;
}

function typeOf(options: Record<string, ParseArgsOptionConfig>, name: string): string {
  const o = optOf(options, name);
  return o !== null ? o.type : "";
}

function longForShort(short: string, options: Record<string, ParseArgsOptionConfig>): string {
  for (const name of Object.keys(options)) {
    const s = options[name].short;
    if (s !== null && s !== undefined && s === short) return name;
  }
  return short;
}

function optionToken(index: int, name: string, rawName: string, value: string | null, inlineValue: boolean | null): ParseArgsToken {
  const t = new ParseArgsToken("option", index);
  t.name = name;
  t.rawName = rawName;
  t.value = value;
  t.inlineValue = inlineValue;
  return t;
}

function positionalToken(index: int, value: string): ParseArgsToken {
  const t = new ParseArgsToken("positional", index);
  t.value = value;
  return t;
}

function argsToTokens(args: string[], options: Record<string, ParseArgsOptionConfig>): ParseArgsToken[] {
  const tokens: ParseArgsToken[] = [];
  let index: int = -1;
  let groupCount: int = 0;
  const remaining: string[] = args.slice();
  while (remaining.length > 0) {
    const arg = remaining.shift() as string;
    const hasNext = remaining.length > 0;
    if (groupCount > 0) groupCount--;
    else index++;
    if (arg === "--") {
      tokens.push(new ParseArgsToken("option-terminator", index));
      for (const rest of remaining) tokens.push(positionalToken(++index, rest));
      break;
    }
    const dash = arg.startsWith("-");
    const dashDash = arg.startsWith("--");
    if (arg.length === 2 && dash && !dashDash) {
      // -f
      const short = arg.slice(1, 2);
      const long = longForShort(short, options);
      let value: string | null = null;
      let inline: boolean | null = null;
      if (typeOf(options, long) === "string" && hasNext) {
        value = remaining.shift() as string;
        inline = false;
      }
      tokens.push(optionToken(index, long, arg, value, inline));
      if (value !== null) ++index;
      continue;
    }
    if (arg.length > 2 && dash && !dashDash) {
      const long = longForShort(arg.slice(1, 2), options);
      if (typeOf(options, long) !== "string") {
        // -abc: -a -b -c (a string option takes the rest: -abfFILE is -a -b -fFILE)
        const expanded: string[] = [];
        for (let i: int = 1; i < arg.length; i++) {
          const s = arg.slice(i, i + 1);
          const l = longForShort(s, options);
          if (typeOf(options, l) !== "string" || i === arg.length - 1) expanded.push(`-${s}`);
          else {
            expanded.push(`-${arg.slice(i)}`);
            break;
          }
        }
        for (let i = expanded.length - 1; i >= 0; i--) remaining.unshift(expanded[i]);
        groupCount = expanded.length;
        continue;
      }
      // -fFILE
      tokens.push(optionToken(index, long, `-${arg.slice(1, 2)}`, arg.slice(2), true));
      continue;
    }
    if (arg.length > 2 && dashDash) {
      const eq = arg.indexOf("=", 3);
      if (eq < 0) {
        // --foo
        const long = arg.slice(2);
        let value: string | null = null;
        let inline: boolean | null = null;
        if (typeOf(options, long) === "string" && hasNext) {
          value = remaining.shift() as string;
          inline = false;
        }
        tokens.push(optionToken(index, long, arg, value, inline));
        if (value !== null) ++index;
        continue;
      }
      // --foo=bar
      const long = arg.slice(2, eq);
      tokens.push(optionToken(index, long, `--${long}`, arg.slice(eq + 1), true));
      continue;
    }
    tokens.push(positionalToken(index, arg));
  }
  return tokens;
}

function unknownOption(rawName: string, allowPositionals: boolean): ParseArgsError {
  const hint = allowPositionals
    ? `. To specify a positional argument starting with a '-', place it at the end of the command after '--', as in '-- ${JSON.stringify(rawName)}`
    : "";
  return new ParseArgsError("ERR_PARSE_ARGS_UNKNOWN_OPTION", `Unknown option '${rawName}'${hint}`);
}

function checkOptionUsage(options: Record<string, ParseArgsOptionConfig>, allowNegative: boolean, allowPositionals: boolean, token: ParseArgsToken): void {
  let name = token.name as string;
  if (!(name in options)) {
    if (allowNegative && name.startsWith("no-")) {
      name = name.slice(3);
      if (!(name in options) || typeOf(options, name) !== "boolean") throw unknownOption(token.rawName as string, allowPositionals);
    } else {
      throw unknownOption(token.rawName as string, allowPositionals);
    }
  }
  const o = options[name] as ParseArgsOptionConfig;
  const short = o.short;
  const shortAndLong = `${short !== null && short !== undefined && short !== "" ? `-${short}, ` : ""}--${name}`;
  if (o.type === "string" && token.value === null)
    throw new ParseArgsError("ERR_PARSE_ARGS_INVALID_OPTION_VALUE", `Option '${shortAndLong} <value>' argument missing`);
  if (o.type === "boolean" && token.value !== null)
    throw new ParseArgsError("ERR_PARSE_ARGS_INVALID_OPTION_VALUE", `Option '${shortAndLong}' does not take an argument`);
}

function checkOptionLikeValue(token: ParseArgsToken): void {
  const v = token.value;
  if (token.inlineValue !== true && v !== null && v.length > 1 && v.startsWith("-")) {
    const raw = token.rawName as string;
    const example = raw.startsWith("--") ? `'${raw}=-XYZ'` : `'--${token.name as string}=-XYZ' or '${raw}-XYZ'`;
    throw new ParseArgsError(
      "ERR_PARSE_ARGS_INVALID_OPTION_VALUE",
      `Option '${raw}' argument is ambiguous.\nDid you forget to specify the option argument for '${raw}'?\nTo specify an option argument starting with a dash use ${example}.`,
    );
  }
}

function storeOption(name: string, value: string | null, options: Record<string, ParseArgsOptionConfig>, allowNegative: boolean, values: Record<string, any>): void {
  if (name === "__proto__") return;
  let long = name;
  let v: any = value !== null ? value : true;
  if (allowNegative && value === null && long.startsWith("no-") && typeOf(options, long.slice(3)) === "boolean") {
    long = long.slice(3);
    v = false;
  }
  const o = optOf(options, long);
  if (o !== null && o.multiple === true) {
    if (!(long in values)) values[long] = [v];
    else (values[long] as any[]).push(v);
  } else {
    values[long] = v;
  }
}

/** Command-line arguments parsed into options and positionals, as Node's util.parseArgs. */
export function parseArgs(config: ParseArgsConfig | null = null): ParsedResults {
  const cfg: ParseArgsConfig = config !== null ? config : {};
  const args: string[] = cfg.args !== null && cfg.args !== undefined ? (cfg.args as string[]) : process.argv.slice(2);
  const strict = cfg.strict !== null && cfg.strict !== undefined ? (cfg.strict as boolean) : true;
  const allowPositionals = cfg.allowPositionals !== null && cfg.allowPositionals !== undefined ? (cfg.allowPositionals as boolean) : !strict;
  const allowNegative = cfg.allowNegative === true;
  const options: Record<string, ParseArgsOptionConfig> = cfg.options !== null && cfg.options !== undefined ? (cfg.options as Record<string, ParseArgsOptionConfig>) : {};
  for (const name of Object.keys(options)) {
    const o = options[name] as ParseArgsOptionConfig;
    if (o.type !== "string" && o.type !== "boolean")
      throw new ParseArgsError(
        "ERR_INVALID_ARG_VALUE",
        `The property 'options.${name}.type' must be one of: 'string', 'boolean'. Received ${inspect(o.type)}`,
      );
    const s = o.short;
    if (s !== null && s !== undefined && s.length !== 1)
      throw new ParseArgsError("ERR_INVALID_ARG_VALUE", `The property 'options.${name}.short' must be a single character. Received ${inspect(s)}`);
  }
  const tokens = argsToTokens(args, options);
  const result = new ParsedResults();
  if (cfg.tokens === true) result.tokens = tokens;
  for (const token of tokens) {
    if (token.kind === "option") {
      if (strict) {
        checkOptionUsage(options, allowNegative, allowPositionals, token);
        checkOptionLikeValue(token);
      }
      storeOption(token.name as string, token.value, options, allowNegative, result.values);
    } else if (token.kind === "positional") {
      if (!allowPositionals)
        throw new ParseArgsError(
          "ERR_PARSE_ARGS_UNEXPECTED_POSITIONAL",
          `Unexpected argument '${token.value as string}'. This command does not take positional arguments`,
        );
      result.positionals.push(token.value as string);
    }
  }
  for (const name of Object.keys(options)) {
    const d: any = options[name].default;
    if (d !== null && d !== undefined && !(name in result.values)) result.values[name] = d;
  }
  return result;
}

// ---- styleText ----

const styleNames: string[] = [
  "reset", "bold", "dim", "italic", "underline", "blink", "inverse", "hidden", "strikethrough", "doubleunderline",
  "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
  "bgBlack", "bgRed", "bgGreen", "bgYellow", "bgBlue", "bgMagenta", "bgCyan", "bgWhite",
  "framed", "overlined", "gray", "redBright", "greenBright", "yellowBright", "blueBright", "magentaBright", "cyanBright", "whiteBright",
  "bgGray", "bgRedBright", "bgGreenBright", "bgYellowBright", "bgBlueBright", "bgMagentaBright", "bgCyanBright", "bgWhiteBright",
  "grey", "blackBright", "bgGrey", "bgBlackBright", "faint", "crossedout", "strikeThrough", "crossedOut", "conceal", "swapColors",
  "swapcolors", "doubleUnderline",
];
const styleOpen: int[] = [
  0, 1, 2, 3, 4, 5, 7, 8, 9, 21, 30, 31, 32, 33, 34, 35, 36, 37, 40, 41, 42, 43, 44, 45, 46, 47, 51, 53, 90, 91, 92, 93, 94, 95, 96, 97,
  100, 101, 102, 103, 104, 105, 106, 107, 90, 90, 100, 100, 2, 9, 9, 9, 8, 7, 7, 21,
];
const styleClose: int[] = [
  0, 22, 22, 23, 24, 25, 27, 28, 29, 24, 39, 39, 39, 39, 39, 39, 39, 39, 49, 49, 49, 49, 49, 49, 49, 49, 54, 55, 39, 39, 39, 39, 39, 39,
  39, 39, 49, 49, 49, 49, 49, 49, 49, 49, 39, 39, 49, 49, 22, 29, 29, 29, 28, 27, 27, 24,
];

/** styleText's options: validateStream (default true: no styling unless stdout is a terminal or FORCE_COLOR says so). */
export interface StyleTextOptions {
  validateStream?: boolean;
  stream?: any;
}

// Node's shouldColorize(process.stdout): FORCE_COLOR decides when set, else a terminal not marked NO_COLOR / dumb
function shouldColorize(fd: int): boolean {
  const force = process.env["FORCE_COLOR"];
  if (force !== undefined && force !== null) {
    return force === "" || force === "1" || force === "true" || force === "2" || force === "3";
  }
  if (!util_isatty(fd)) return false;
  const nc = process.env["NO_COLOR"];
  const ndc = process.env["NODE_DISABLE_COLORS"];
  if ((nc !== undefined && nc !== null) || (ndc !== undefined && ndc !== null) || process.env["TERM"] === "dumb") return false;
  return true;
}

/** The text with the ANSI styles named (one name or a list), as Node's util.styleText. */
export function styleText(format: string | string[], text: string, options: StyleTextOptions | null = null): string {
  const validate = options === null || options.validateStream !== false;
  const skip = validate && !shouldColorize(1);
  const keys: string[] = typeof format === "string" ? [format as string] : (format as string[]);
  let left = "";
  let right = "";
  for (const key of keys) {
    if (key === "none") continue;
    const at = styleNames.indexOf(key);
    if (at < 0) {
      throw new TypeError(
        `The argument 'format' must be one of: ${styleNames.map((n: string): string => `'${n}'`).join(", ")}. Received ${inspect(key)}`,
      );
    }
    if (skip) continue;
    left += `\u001b[${styleOpen[at]}m`;
    right = `\u001b[${styleClose[at]}m${right}`;
  }
  return skip ? text : `${left}${text}${right}`;
}

// ---- stripVTControlCharacters ----

function isParamChar(c: number): boolean {
  // [-a-zA-Z\d\/\#&.:=?%@~_]
  return (
    (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) ||
    c === 45 || c === 47 || c === 35 || c === 38 || c === 46 || c === 58 || c === 61 || c === 63 || c === 37 || c === 64 || c === 126 || c === 95
  );
}
function isAlnum(c: number): boolean {
  return (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122);
}
function isDigit(c: number): boolean {
  return c >= 48 && c <= 57;
}
// the final byte of a CSI sequence: [\dA-PR-TZcf-ntqry=><~]
function isFinal(c: number): boolean {
  return (
    isDigit(c) || (c >= 65 && c <= 80) || (c >= 82 && c <= 84) || c === 90 || c === 99 || (c >= 102 && c <= 110) ||
    c === 116 || c === 113 || c === 114 || c === 121 || c === 61 || c === 62 || c === 60 || c === 126
  );
}

// the end of an OSC-like sequence at i: (;[param]+)* or [alnum]+(;[param]*)*, then BEL, ESC \ or ST; -1 if none
function oscEnd(s: string, i: int): int {
  const n = s.length;
  const term = (j: int): int => {
    if (j < n && s.charCodeAt(j) === 7) return j + 1;
    if (j + 1 < n && s.charCodeAt(j) === 27 && s.charCodeAt(j + 1) === 92) return j + 2;
    if (j < n && s.charCodeAt(j) === 0x9c) return j + 1;
    return -1;
  };
  // the pattern's alternatives, each tried with every length the regular expression could back off to; the
  // terminators are not parameter characters, so the greedy run is the only one that can reach one
  let j: int = i;
  if (j < n && s.charCodeAt(j) === 59) {
    while (j < n && s.charCodeAt(j) === 59) {
      let k: int = j + 1;
      while (k < n && isParamChar(s.charCodeAt(k))) k++;
      if (k === j + 1) break;
      j = k;
    }
  } else if (j < n && isAlnum(s.charCodeAt(j))) {
    while (j < n && isAlnum(s.charCodeAt(j))) j++;
    while (j < n && s.charCodeAt(j) === 59) {
      j++;
      while (j < n && isParamChar(s.charCodeAt(j))) j++;
    }
  }
  const e = term(j);
  if (e >= 0) return e;
  return term(i);
}

// the end of a CSI-like sequence at i: (\d{1,4}(;\d{0,4})*)? then a final character; -1 if none. The final
// characters include the digits, so backing off from the longest run of parameters finds the regular expression's match.
function csiEnd(s: string, i: int): int {
  const n = s.length;
  let j: int = i;
  let d: int = 0;
  while (j < n && d < 4 && isDigit(s.charCodeAt(j))) {
    j++;
    d++;
  }
  if (d > 0) {
    while (j < n && s.charCodeAt(j) === 59) {
      let k: int = j + 1;
      let e: int = 0;
      while (k < n && e < 4 && isDigit(s.charCodeAt(k))) {
        k++;
        e++;
      }
      j = k;
    }
  }
  for (let p = j; p >= i; p--) if (p < n && isFinal(s.charCodeAt(p))) return p + 1;
  return -1;
}

/** The string without ANSI escape sequences (colors, cursor moves, hyperlinks). */
export function stripVTControlCharacters(str: string): string {
  let out = "";
  let last: int = 0;
  let i: int = 0;
  const n = str.length;
  while (i < n) {
    const c = str.charCodeAt(i);
    if (c === 27 || c === 0x9b) {
      let j: int = i + 1;
      while (j < n) {
        const p = str.charCodeAt(j);
        if (p === 91 || p === 93 || p === 40 || p === 41 || p === 35 || p === 59 || p === 63) j++;
        else break;
      }
      // the regular expression backs off over [[\]()#;?]* too: try each start from the longest run down
      let end: int = -1;
      for (let k = j; k >= i + 1 && end < 0; k--) {
        end = oscEnd(str, k);
        if (end < 0) end = csiEnd(str, k);
      }
      if (end >= 0) {
        out += str.slice(last, i);
        last = end;
        i = end;
        continue;
      }
    }
    i++;
  }
  return last === 0 ? str : out + str.slice(last);
}

/** The string with lone surrogates replaced by U+FFFD. */
export function toUSVString(str: string): string {
  let out = "";
  let last: int = 0;
  const n = str.length;
  for (let i: int = 0; i < n; i++) {
    const c = str.charCodeAt(i);
    if (c < 0xd800 || c > 0xdfff) continue;
    if (c <= 0xdbff && i + 1 < n) {
      const d = str.charCodeAt(i + 1);
      if (d >= 0xdc00 && d <= 0xdfff) {
        i++;
        continue;
      }
    }
    out += str.slice(last, i) + "\ufffd";
    last = i + 1;
  }
  return last === 0 ? str : out + str.slice(last);
}

/** The name of a system error number (negative, as libuv gives them): -2 is ENOENT. */
export function getSystemErrorName(err: int): string {
  if (err >= 0) throw new RangeError(`The value of "err" is out of range. It must be a negative integer. Received ${err}`);
  const n = util_errname(-err);
  return n !== "" ? n : `Unknown system error ${err}`;
}

/** Whether the value is an array (deprecated in Node: Array.isArray). */
export function isArray(v: any): boolean {
  return Array.isArray(v);
}

// TextEncoder and TextDecoder: the built-in classes (the checker lets them be imported from here)

/** The module as one object: import util from "node:util"; util.format(...). */
export class Util {
  types: UtilTypes = types;
  format(...args: any[]): string {
    return formatArgs(2, 80, 3, args);
  }
  formatWithOptions(options: InspectOptions, ...args: any[]): string {
    const d = options.depth;
    const b = options.breakLength;
    return formatArgs(d !== null && d !== undefined ? optInt(d, 2) : 2, b !== null && b !== undefined ? optInt(b, 80) : 80, compactOf(options.compact), args);
  }
  inspect(value: any, options: InspectOptions | null = null): string {
    return inspect(value, options);
  }
  isDeepStrictEqual(a: any, b: any): boolean {
    return util_deep_equal(a, b);
  }
  deprecate(fn: (...args: any[]) => any, msg: string, code: string | null = null): (...args: any[]) => any {
    return deprecate(fn, msg, code);
  }
  debuglog(section: string): (...args: any[]) => void {
    return debuglog(section);
  }
  parseArgs(config: ParseArgsConfig | null = null): ParsedResults {
    return parseArgs(config);
  }
  styleText(format: string | string[], text: string, options: StyleTextOptions | null = null): string {
    return styleText(format, text, options);
  }
  stripVTControlCharacters(str: string): string {
    return stripVTControlCharacters(str);
  }
  toUSVString(str: string): string {
    return toUSVString(str);
  }
  getSystemErrorName(err: int): string {
    return getSystemErrorName(err);
  }
  isArray(v: any): boolean {
    return Array.isArray(v);
  }
}

const util: Util = new Util();
export default util;
