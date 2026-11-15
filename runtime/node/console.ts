// xisto:console: the console methods beyond log/info/debug/error/warn, written in xisto. The compiler reads
// console.X as XistoConsole.X for them (src/checker.c) and loads this module when the program names one
// (src/parser.c). console.log & co stay the compiler's own (its stdout fast path); in a program that names
// console.group or console.groupCollapsed, each of them is emitted between xs_grp_begin and xs_grp_end, which indent
// its lines by the depth this module keeps (console_group_indent).
// Differences from Node (docs/docs/guides/node-compatibility.md): console.table shows arrays and objects (not Maps
// or Sets) and inspects nested values as Node does (depth 0, three array items); console.trace prints xisto's stack;
// the warnings of time/timeEnd/countReset for a label that is not there are written as Node's process warnings.

import { format } from "xisto:util";

declare function util_inspect(v: any, depth: int, breakLength: int, compact: int): string;
declare function util_kind(v: any): int;
declare function util_keys(v: any): string[];
declare function console_group_indent(n: int): void;

const K_NULL: int = 0;
const K_STRING: int = 1;
const K_NUMBER: int = 2;
const K_BOOLEAN: int = 3;
const K_ARRAY: int = 4;
const K_FUNCTION: int = 10;
const K_SYMBOL: int = 11;
const K_BIGINT: int = 12;
const BIG: int = 1000000000;

let indent = "";
const counts: Map<string, int> = new Map();
const timers: Map<string, number> = new Map();

function out(err: boolean, text: string): void {
  let s = text;
  if (indent.length > 0) {
    if (s.indexOf("\n") >= 0) s = s.split("\n").join("\n" + indent);
    s = indent + s;
  }
  if (err) process.stderr.write(s + "\n");
  else process.stdout.write(s + "\n");
}

// a process warning, as Node writes it: after the current task (process.emitWarning defers it), the first one followed
// by the hint about --trace-warnings
let warned = false;
function warning(text: string): void {
  queueMicrotask(() => {
    process.stderr.write(`(node:${process.pid}) Warning: ${text}\n`);
    if (!warned) process.stderr.write("(Use `node --trace-warnings ...` to show where the warning was created)\n");
    warned = true;
  });
}

// Node's formatTime: "1.234ms", "1.500s", "1:02.345 (m:ss.mmm)"
function pad2(n: number): string {
  const s = `${n}`;
  return s.length < 2 ? "0" + s : s;
}
function formatTime(msIn: number): string {
  let ms = msIn;
  let hours = 0;
  let minutes = 0;
  let seconds = 0;
  if (ms >= 1000) {
    if (ms >= 60000) {
      if (ms >= 3600000) {
        hours = Math.floor(ms / 3600000);
        ms = ms % 3600000;
      }
      minutes = Math.floor(ms / 60000);
      ms = ms % 60000;
    }
    seconds = ms / 1000;
  }
  if (hours !== 0 || minutes !== 0) {
    const parts = seconds.toFixed(3).split(".");
    const res = hours !== 0 ? `${hours}:${pad2(minutes)}` : `${minutes}`;
    const sec = parts[0].length < 2 ? "0" + parts[0] : parts[0];
    return `${res}:${sec}.${parts[1]} (${hours !== 0 ? "h:m" : ""}m:ss.mmm)`;
  }
  if (seconds !== 0) return `${seconds.toFixed(3)}s`;
  return `${Number(ms.toFixed(3))}ms`;
}

// the columns a string takes in a terminal (Node's getStringWidth): wide East Asian characters 2, combining marks 0
function isFullWidth(c: int): boolean {
  return (
    c >= 0x1100 &&
    (c <= 0x115f ||
      c === 0x2329 ||
      c === 0x232a ||
      (c >= 0x2e80 && c <= 0x3247 && c !== 0x303f) ||
      (c >= 0x3250 && c <= 0x4dbf) ||
      (c >= 0x4e00 && c <= 0xa4c6) ||
      (c >= 0xa960 && c <= 0xa97c) ||
      (c >= 0xac00 && c <= 0xd7a3) ||
      (c >= 0xf900 && c <= 0xfaff) ||
      (c >= 0xfe10 && c <= 0xfe19) ||
      (c >= 0xfe30 && c <= 0xfe6b) ||
      (c >= 0xff01 && c <= 0xff60) ||
      (c >= 0xffe0 && c <= 0xffe6) ||
      (c >= 0x1b000 && c <= 0x1b001) ||
      (c >= 0x1f200 && c <= 0x1f251) ||
      (c >= 0x1f300 && c <= 0x1f64f) ||
      (c >= 0x20000 && c <= 0x3fffd))
  );
}
function isZeroWidth(c: int): boolean {
  return (
    c <= 0x1f ||
    (c >= 0x7f && c <= 0x9f) ||
    (c >= 0x300 && c <= 0x36f) ||
    (c >= 0x200b && c <= 0x200f) ||
    (c >= 0x20d0 && c <= 0x20ff) ||
    (c >= 0xfe00 && c <= 0xfe0f) ||
    (c >= 0xfe20 && c <= 0xfe2f) ||
    (c >= 0xe0100 && c <= 0xe01ef)
  );
}
function stringWidth(s: string): int {
  let w: int = 0;
  let i: int = 0;
  while (i < s.length) {
    const c = Math.floor(s.codePointAt(i)!) as int;
    i += c > 0xffff ? 2 : 1;
    if (isZeroWidth(c)) continue;
    w += isFullWidth(c) ? 2 : 1;
  }
  return w;
}

function renderRow(row: string[], widths: int[]): string {
  let s = "│ ";
  for (let i: int = 0; i < row.length; i++) {
    const cell = row[i];
    s += cell + " ".repeat(widths[i] - stringWidth(cell));
    if (i !== row.length - 1) s += " │ ";
  }
  return s + " │";
}

function renderTable(head: string[], columns: string[][]): string {
  const widths: int[] = [];
  for (const h of head) widths.push(stringWidth(h));
  let longest: int = 0;
  for (const c of columns) if (c.length > longest) longest = c.length;
  const rows: string[][] = [];
  for (let j: int = 0; j < longest; j++) rows.push([]);
  for (let i: int = 0; i < head.length; i++) {
    const column = columns[i];
    for (let j: int = 0; j < longest; j++) {
      const v = j < column.length ? column[j] : "";
      rows[j].push(v);
      const w = stringWidth(v);
      if (w > widths[i]) widths[i] = w;
    }
  }
  const divider: string[] = [];
  for (const w of widths) divider.push("─".repeat(w + 2));
  let r = "┌" + divider.join("┬") + "┐\n" + renderRow(head, widths) + "\n" + "├" + divider.join("┼") + "┤\n";
  for (const row of rows) r += renderRow(row, widths) + "\n";
  return r + "└" + divider.join("┴") + "┘";
}

function isPrimitive(v: any): boolean {
  const k = util_kind(v);
  return k === K_NULL || k === K_STRING || k === K_NUMBER || k === K_BOOLEAN || k === K_SYMBOL || k === K_BIGINT;
}

// a cell: inspected with depth 0 (an object of more than two keys collapsed), three array items at most, on one line
function cell(v: any): string {
  const k = util_kind(v);
  if (k === K_ARRAY) {
    const n = util_keys(v).length;
    if (n <= 3) return util_inspect(v, 0, BIG, 3);
    const first: any[] = [v[0], v[1], v[2]];
    const s = util_inspect(first, 0, BIG, 3);
    const more = n - 3;
    return s.slice(0, s.length - 2) + `, ... ${more} more item${more > 1 ? "s" : ""} ]`;
  }
  if (!isPrimitive(v) && k !== K_FUNCTION && util_keys(v).length > 2) return util_inspect(v, -1, BIG, 3);
  return util_inspect(v, 0, BIG, 3);
}

// v[key] of an array (key an index) or an object
function member(v: any, key: string): any {
  if (util_kind(v) === K_ARRAY) return v[Math.floor(Number(key)) as int];
  return v[key];
}

function tableOf(data: any, properties: string[] | null): string | null {
  if (isPrimitive(data) || util_kind(data) === K_FUNCTION) return null;
  const indexKeys = util_keys(data);
  const keys: string[] = [];
  const cols: Map<string, string[]> = new Map();
  let hasPrimitives = false;
  const values: string[] = [];
  for (let i: int = 0; i < indexKeys.length; i++) {
    const item: any = member(data, indexKeys[i]);
    const primitive = isPrimitive(item);
    if (properties === null && primitive) {
      hasPrimitives = true;
      while (values.length < i) values.push("");
      values.push(cell(item));
    } else {
      const own = primitive ? [] : util_keys(item);
      const ks = properties !== null ? properties : own;
      for (const key of ks) {
        let col = cols.get(key);
        if (col === undefined) {
          col = [];
          cols.set(key, col);
          keys.push(key);
        }
        while (col.length < i) col.push("");
        col.push(primitive || !own.includes(key) ? "" : cell(member(item, key)));
      }
    }
  }
  const head: string[] = ["(index)"];
  const columns: string[][] = [indexKeys];
  for (const key of keys) {
    head.push(key);
    columns.push(cols.get(key)!);
  }
  if (hasPrimitives) {
    head.push("Values");
    columns.push(values);
  }
  return renderTable(head, columns);
}

/** console.dir's options: depth (null: all levels); the others are accepted. */
export interface DirOptions {
  depth?: number | null;
  colors?: boolean;
  showHidden?: boolean;
}

/** console's methods beyond log and its kin (the compiler reads console.X as XistoConsole.X). */
export class XistoConsole {
  static dir(value: any, options: DirOptions | null = null): void {
    let depth: int = 2;
    if (options !== null) {
      const d = options.depth;
      if (d === null) depth = BIG;
      else if (d !== undefined) depth = d > BIG ? BIG : (Math.floor(d) as int);
    }
    out(false, util_inspect(value, depth, 80, 3));
  }
  static dirxml(...args: any[]): void {
    out(false, format(...args));
  }
  static table(data: any, properties: string[] | null = null): void {
    const t = tableOf(data, properties);
    out(false, t === null ? format(data) : t);
  }
  static group(...label: any[]): void {
    if (label.length > 0) out(false, format(...label));
    indent += "  ";
    console_group_indent(indent.length);
  }
  static groupCollapsed(...label: any[]): void {
    if (label.length > 0) out(false, format(...label));
    indent += "  ";
    console_group_indent(indent.length);
  }
  static groupEnd(): void {
    indent = indent.slice(0, indent.length >= 2 ? indent.length - 2 : 0);
    console_group_indent(indent.length);
  }
  static count(label: string = "default"): void {
    const n = (counts.get(label) ?? 0) + 1;
    counts.set(label, n);
    out(false, `${label}: ${n}`);
  }
  static countReset(label: string = "default"): void {
    if (!counts.has(label)) {
      warning(`Count for '${label}' does not exist`);
      return;
    }
    counts.delete(label);
  }
  static time(label: string = "default"): void {
    if (timers.has(label)) {
      warning(`Label '${label}' already exists for console.time()`);
      return;
    }
    timers.set(label, performance.now());
  }
  static timeEnd(label: string = "default"): void {
    const t0 = timers.get(label);
    if (t0 === undefined) {
      warning(`No such label '${label}' for console.timeEnd()`);
      return;
    }
    timers.delete(label);
    out(false, `${label}: ${formatTime(performance.now() - t0)}`);
  }
  static timeLog(label: string = "default", ...data: any[]): void {
    const t0 = timers.get(label);
    if (t0 === undefined) {
      warning(`No such label '${label}' for console.timeLog()`);
      return;
    }
    const head = `${label}: ${formatTime(performance.now() - t0)}`;
    out(false, data.length > 0 ? head + " " + format(...data) : head);
  }
  static assert(value: any, ...args: any[]): void {
    if (value) return;
    if (args.length === 0) {
      out(true, "Assertion failed");
      return;
    }
    const rest: any[] = [];
    rest.push(`Assertion failed: ${args[0]}`);
    for (let i: int = 1; i < args.length; i++) rest.push(args[i]);
    out(true, format(...rest));
  }
  static trace(...args: any[]): void {
    const stack = new Error("").stack ?? "";
    let nl = stack.indexOf("\n"); // (the stack after its "Error" line, from the caller's frame on)
    if (nl >= 0 && stack.indexOf("XistoConsole", nl) === nl + 8) nl = stack.indexOf("\n", nl + 1);
    out(true, "Trace" + (args.length > 0 ? ": " + format(...args) : "") + (nl >= 0 ? stack.slice(nl) : ""));
  }
}
