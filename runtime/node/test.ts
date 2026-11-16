// xisto:test (node:test), written in xisto: test/it, describe/suite, before/after/beforeEach/afterEach, the skip,
// todo and only options and forms (test.skip, it.todo, describe.only...), async tests, subtests (t.test),
// t.diagnostic, t.skip, t.todo. A program that registers tests runs them after its module body, one at a time, and
// prints Node's default (spec) reporter to stdout: "✔ name (1.2ms)", "▶ suite", the "ℹ tests 1" summary and the
// failing tests with their errors; the exit status is 1 when a test failed (process.exitCode).
// The compiler reads test.skip(...) (and it/describe/suite .skip/.todo/.only) of a named import as __test_skip(...)
// & co (src/parser.c).
// `xisto test` builds and runs each *.test.ts as a program: a file that uses node:test fails there when one of its
// tests fails (exit status 1), and its output is this reporter's.
// Differences from Node (docs/docs/guides/node-compatibility.md): the errors of failed tests show xisto's stack
// frames; the reporter's lines are written when the program yields to the event loop (Node's stream may group them
// differently around a test's own console output in some programs); timeout, concurrency, plan, mock and t.assert are not provided
// (timeout and concurrency are accepted); subtests must be awaited; the done-callback form is not supported.

import { inspect } from "xisto:util";

declare function util_kind(v: any): int;
declare function util_inspect(v: any, depth: int, breakLength: int, compact: int): string;

const K_STRING: int = 1;

/** The options of test(), it(), describe(). */
export interface TestOptions {
  skip?: boolean | string;
  todo?: boolean | string;
  only?: boolean;
  timeout?: number;
  concurrency?: number | boolean;
  signal?: AbortSignal;
}

export type TestFn = (t: TestContext) => void | Promise<void>;
export type SuiteFn = (s: SuiteContext) => void | Promise<void>;
export type HookFn = (t: TestContext) => void | Promise<void>;

class TNode {
  name: string;
  fn: TestFn | null;
  sfn: SuiteFn | null = null;
  suite: boolean;
  parent: TNode | null;
  children: TNode[] = [];
  skip: string | null = null; // the reason ("" for none)
  todo: string | null = null;
  only: boolean = false;
  loc: string;
  depth: int;
  before: HookFn[] = [];
  after: HookFn[] = [];
  beforeEach: HookFn[] = [];
  afterEach: HookFn[] = [];
  headed: boolean = false; // its "▶ name" line was written
  failed: boolean = false;
  diags: string[] = [];
  constructor(name: string, fn: TestFn | null, suite: boolean, parent: TNode | null, loc: string) {
    this.name = name;
    this.fn = fn;
    this.suite = suite;
    this.parent = parent;
    this.loc = loc;
    this.depth = parent === null ? -1 : parent.depth + 1;
  }
}

class Failure {
  node: TNode;
  line: string;
  error: any;
  raw: any;
  constructor(node: TNode, line: string, error: any, raw: any) {
    this.node = node;
    this.line = line;
    this.error = error;
    this.raw = raw;
  }
}

const root = new TNode("<root>", null, true, null, "");
let current: TNode = root;
let scheduled = false;
let running = false;
let started = false;
let done = 0; // root's children run so far
let t0 = 0;
let pending = "";
let flushQueued = false;
let nTests: int = 0;
let nSuites: int = 0;
let nPass: int = 0;
let nFail: int = 0;
let nSkipped: int = 0;
let nTodo: int = 0;
const failures: Failure[] = [];

// ---- the reporter: lines buffered until the program first yields to the event loop (all of them, when no test
// waits on a timer or I/O), then written as they come, as Node's reporter stream does ----
let flowing = false;
function flush(): void {
  flushQueued = false;
  flowing = true;
  if (pending.length > 0) process.stdout.write(pending);
  pending = "";
}
function emit(line: string): void {
  if (flowing) {
    process.stdout.write(line + "\n");
    return;
  }
  pending += line + "\n";
  if (!flushQueued) {
    flushQueued = true;
    setImmediate(flush);
  }
}
function pad(depth: int): string {
  return depth > 0 ? "  ".repeat(depth) : "";
}
function header(n: TNode | null): void {
  // the "▶ name" lines of the suites and tests above a result, once each
  if (n === null || n.depth < 0 || n.headed) return;
  header(n.parent);
  n.headed = true;
  emit(pad(n.depth) + "▶ " + n.name);
}
function ms(d: number): string {
  return `(${d}ms)`;
}

// where test() was called: the first frame of the program's own code in the stack
function callerLoc(): string {
  const stack = new Error("").stack ?? "";
  const lines = stack.split("\n");
  for (let i: int = 1; i < lines.length; i++) {
    const l = lines[i].trim();
    if (!l.startsWith("at ") || l.indexOf("xisto:") >= 0) continue;
    const open = l.lastIndexOf("(");
    if (open >= 0 && l.endsWith(")")) return l.slice(open + 1, l.length - 1);
    return l.slice(3);
  }
  return "";
}

// ---- registering ----
function schedule(): void {
  if (scheduled) return;
  scheduled = true;
  if (!started) {
    started = true;
    t0 = performance.now();
    process.on("beforeExit", finish);
  }
  setImmediate(() => {
    scheduled = false;
    void runRoot();
  });
}
function optionsOf(n: TNode, o: TestOptions | null): void {
  if (o === null) return;
  const s = o.skip;
  if (s !== undefined && s !== false) n.skip = typeof s === "string" ? (s as string) : "";
  const t = o.todo;
  if (t !== undefined && t !== false) n.todo = typeof t === "string" ? (t as string) : "";
  if (o.only === true) n.only = true;
}
function add(name: string, a: TestOptions | TestFn | null, b: TestFn | null, suite: boolean, mode: string, loc: string): TNode {
  let opts: TestOptions | null = null;
  let fn: TestFn | null = b;
  if (a !== null) {
    if (typeof a === "function") fn = a as TestFn;
    else opts = a as TestOptions;
  }
  const n = new TNode(name, suite ? null : fn, suite, current, loc);
  optionsOf(n, opts);
  if (mode === "skip") n.skip = "";
  else if (mode === "todo") n.todo = "";
  else if (mode === "only") n.only = true;
  current.children.push(n);
  if (current === root) schedule();
  return n;
}
function addSuite(name: string, a: TestOptions | SuiteFn | null, b: SuiteFn | null, mode: string, loc: string): void {
  let opts: TestOptions | null = null;
  let fn: SuiteFn | null = b;
  if (a !== null) {
    if (typeof a === "function") fn = a as SuiteFn;
    else opts = a as TestOptions;
  }
  const n = add(name, opts, null, true, mode, loc);
  n.sfn = fn;
  if (fn === null || n.skip !== null) return;
  // (the suite's body runs now, as in Node: it registers the suite's tests and hooks)
  const saved = current;
  current = n;
  try {
    const r = fn(new SuiteContext(name));
    void r;
  } finally {
    current = saved;
  }
}

/** A test: test(name, fn), test(name, options, fn). */
export function test(name: string, a: TestOptions | TestFn | null = null, b: TestFn | null = null): void {
  add(name, a, b, false, "", callerLoc());
}
/** it() is test(). */
export function it(name: string, a: TestOptions | TestFn | null = null, b: TestFn | null = null): void {
  add(name, a, b, false, "", callerLoc());
}
/** A suite: its body runs now and registers its tests and hooks. */
export function describe(name: string, a: TestOptions | SuiteFn | null = null, b: SuiteFn | null = null): void {
  addSuite(name, a, b, "", callerLoc());
}
/** suite() is describe(). */
export function suite(name: string, a: TestOptions | SuiteFn | null = null, b: SuiteFn | null = null): void {
  addSuite(name, a, b, "", callerLoc());
}
/** test.skip / it.skip (also skip() of the default import). */
export function skip(name: string, a: TestOptions | TestFn | null = null, b: TestFn | null = null): void {
  add(name, a, b, false, "skip", callerLoc());
}
export function todo(name: string, a: TestOptions | TestFn | null = null, b: TestFn | null = null): void {
  add(name, a, b, false, "todo", callerLoc());
}
export function only(name: string, a: TestOptions | TestFn | null = null, b: TestFn | null = null): void {
  add(name, a, b, false, "only", callerLoc());
}
export function __test_skip(name: string, a: TestOptions | TestFn | null = null, b: TestFn | null = null): void {
  add(name, a, b, false, "skip", callerLoc());
}
export function __test_todo(name: string, a: TestOptions | TestFn | null = null, b: TestFn | null = null): void {
  add(name, a, b, false, "todo", callerLoc());
}
export function __test_only(name: string, a: TestOptions | TestFn | null = null, b: TestFn | null = null): void {
  add(name, a, b, false, "only", callerLoc());
}
export function __describe_skip(name: string, a: TestOptions | SuiteFn | null = null, b: SuiteFn | null = null): void {
  addSuite(name, a, b, "skip", callerLoc());
}
export function __describe_todo(name: string, a: TestOptions | SuiteFn | null = null, b: SuiteFn | null = null): void {
  addSuite(name, a, b, "todo", callerLoc());
}
export function __describe_only(name: string, a: TestOptions | SuiteFn | null = null, b: SuiteFn | null = null): void {
  addSuite(name, a, b, "only", callerLoc());
}
/** Runs once before the tests of the suite it is written in (of the file, at the top level). */
export function before(fn: HookFn): void {
  current.before.push(fn);
}
export function after(fn: HookFn): void {
  current.after.push(fn);
}
/** Runs before each test of the suite it is written in, and of its inner suites. */
export function beforeEach(fn: HookFn): void {
  current.beforeEach.push(fn);
}
export function afterEach(fn: HookFn): void {
  current.afterEach.push(fn);
}

/** The context a suite's body receives. */
export class SuiteContext {
  readonly name: string;
  constructor(name: string) {
    this.name = name;
  }
}

/** The context a test receives: its name, subtests, diagnostics, skip and todo. */
export class TestContext {
  readonly name: string;
  readonly node: TNode;
  constructor(node: TNode) {
    this.name = node.name;
    this.node = node;
  }
  /** A subtest: runs now (await it). */
  async test(name: string, a: TestOptions | TestFn | null = null, b: TestFn | null = null): Promise<void> {
    const saved = current;
    current = this.node;
    const n = add(name, a, b, false, "", callerLoc());
    current = saved;
    await runNode(n);
  }
  /** A line printed after the test's result: "ℹ message". */
  diagnostic(message: string): void {
    this.node.diags.push(message);
  }
  /** Marks the test skipped (it still runs to its end). */
  skip(message: string = ""): void {
    this.node.skip = message;
  }
  /** Marks the test as a todo. */
  todo(message: string = ""): void {
    this.node.todo = message;
  }
}

// ---- running ----
function hooksOf(n: TNode | null, before: boolean, out: HookFn[]): void {
  // beforeEach from the outermost suite in; afterEach from the innermost out
  if (n === null) return;
  if (before) {
    hooksOf(n.parent, true, out);
    for (const h of n.beforeEach) out.push(h);
  } else {
    for (const h of n.afterEach) out.push(h);
    hooksOf(n.parent, false, out);
  }
}
function anyOnly(n: TNode): boolean {
  for (const c of n.children) if (c.only || (c.suite && anyOnly(c))) return true;
  return false;
}
function selected(n: TNode): boolean {
  // with test.only somewhere in the parent, the others do not run (and are not reported), as Node does
  const p = n.parent;
  if (p === null || !anyOnly(p)) return true;
  return n.only || (n.suite && anyOnly(n));
}
class Outcome {
  ok: boolean = true;
  error: any = null; // the Error that carries what was thrown (its stack)
  raw: any = null; // what was thrown
}
async function call(fn: HookFn, ctx: TestContext, o: Outcome): Promise<void> {
  if (!o.ok) return;
  try {
    await fn(ctx);
  } catch (e) {
    o.ok = false;
    o.error = e;
    try {
      throw e;
    } catch (r: any) {
      o.raw = r;
    }
  }
}
function suffix(n: TNode): string {
  if (n.skip !== null) return " # " + (n.skip.length > 0 ? n.skip : "SKIP");
  if (n.todo !== null) return " # " + (n.todo.length > 0 ? n.todo : "TODO");
  return "";
}
function report(n: TNode, ok: boolean, d: number, o: Outcome | null, listed: boolean): void {
  header(n.parent);
  const mark = n.skip !== null ? "﹣" : ok ? "✔" : n.todo !== null ? "⚠" : "✖";
  const line = mark + " " + n.name + " " + ms(d) + suffix(n);
  emit(pad(n.depth) + line);
  for (const m of n.diags) emit(pad(n.depth) + "ℹ " + m);
  if (!ok && listed && o !== null) failures.push(new Failure(n, line, o.error, o.raw));
}
async function runNode(n: TNode): Promise<void> {
  if (!selected(n)) return;
  const start = performance.now();
  if (n.suite) {
    nSuites++;
    if (n.skip !== null) {
      report(n, true, performance.now() - start, null, false);
      return;
    }
    const ctx = new TestContext(n);
    const o = new Outcome();
    for (const h of n.before) await call(h, ctx, o);
    if (o.ok) for (let i: int = 0; i < n.children.length; i++) await runNode(n.children[i]);
    for (const h of n.after) await call(h, ctx, o);
    let failed = !o.ok;
    for (const c of n.children) if (c.failed) failed = true;
    n.failed = failed;
    report(n, !failed, performance.now() - start, o, !o.ok);
    if (!o.ok) nFail++;
    return;
  }
  nTests++;
  if (n.skip !== null) {
    nSkipped++;
    report(n, true, performance.now() - start, null, false);
    return;
  }
  const ctx = new TestContext(n);
  const o = new Outcome();
  const pre: HookFn[] = [];
  hooksOf(n.parent, true, pre);
  for (const h of pre) await call(h, ctx, o);
  if (n.fn !== null) await call(n.fn, ctx, o);
  const post: HookFn[] = [];
  hooksOf(n.parent, false, post);
  for (const h of post) {
    if (!o.ok) break;
    await call(h, ctx, o);
  }
  let subFailed = false;
  for (const c of n.children) if (c.failed) subFailed = true;
  const ok = o.ok && !subFailed;
  const d = performance.now() - start;
  if (n.skip !== null) {
    nSkipped++;
    report(n, true, d, null, false);
    return;
  }
  if (n.todo !== null) {
    nTodo++;
    report(n, ok, d, o, !o.ok);
    return;
  }
  n.failed = !ok;
  if (ok) nPass++;
  else nFail++;
  report(n, ok, d, o, !o.ok);
}
async function runRoot(): Promise<void> {
  if (running) return;
  running = true;
  if (root.before.length > 0 && done === 0) {
    const o = new Outcome();
    for (const h of root.before) await call(h, new TestContext(root), o);
  }
  while (done < root.children.length) {
    const n = root.children[done];
    done++;
    await runNode(n);
  }
  running = false;
}

// ---- the end: root's after hooks, the summary and the failing tests ----
let finished = false;
function errorText(e: any, raw: any): string {
  if (!(raw instanceof Error)) return inspect(raw);
  const name = util_kind(e.name) === K_STRING ? (e.name as string) : "Error";
  const msg = util_kind(e.message) === K_STRING ? (e.message as string) : "";
  const isAssert = name === "AssertionError";
  const head = name + (isAssert ? " [ERR_ASSERTION]" : "") + (msg.length > 0 ? ": " + msg : "");
  const stack = util_kind(e.stack) === K_STRING ? (e.stack as string) : "";
  const frames: string[] = [];
  for (const l of stack.split("\n")) {
    const t = l.trim();
    if (t.startsWith("at ") && t.indexOf("xisto:") < 0) frames.push("    " + t);
  }
  let s = head;
  if (frames.length > 0) s += (isAssert && head.endsWith("\n") ? "\n" : "\n") + frames.join("\n");
  if (isAssert) {
    const props = [
      `generatedMessage: ${e.generatedMessage}`,
      "code: 'ERR_ASSERTION'",
      `actual: ${util_inspect(e.actual, -1, 80, 3)}`,
      `expected: ${util_inspect(e.expected, -1, 80, 3)}`,
      `operator: ${inspect(e.operator)}`,
      "diff: 'simple'",
    ];
    s += " {\n  " + props.join(",\n  ") + "\n}";
  }
  return s;
}
function finish(): void {
  if (finished || running) return;
  finished = true;
  flush();
  // (root's after hooks: synchronous ones)
  for (const h of root.after) {
    try {
      void h(new TestContext(root));
    } catch (e) {
      void e;
    }
  }
  const lines: string[] = [
    `ℹ tests ${nTests}`,
    `ℹ suites ${nSuites}`,
    `ℹ pass ${nPass}`,
    `ℹ fail ${nFail}`,
    "ℹ cancelled 0",
    `ℹ skipped ${nSkipped}`,
    `ℹ todo ${nTodo}`,
    `ℹ duration_ms ${performance.now() - t0}`,
  ];
  let out = lines.join("\n") + "\n";
  if (failures.length > 0) {
    out += "\n✖ failing tests:\n";
    for (const f of failures) {
      out += "\ntest at " + f.node.loc + "\n" + f.line + "\n";
      out += "  " + errorText(f.error, f.raw).split("\n").join("\n  ") + "\n";
    }
  }
  process.stdout.write(out);
  if (nFail > 0) process.exitCode = 1;
}
