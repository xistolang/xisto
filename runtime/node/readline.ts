// xisto:readline (node:readline): createInterface over a Readable (process.stdin, fs.createReadStream, any stream),
// with 'line', 'close', 'pause' and 'resume' events, question(), prompt()/setPrompt(), write(), close(), for await
// over the lines, and readline.promises (also node:readline/promises: runtime/node/readline_promises.ts); and the
// cursor helpers (clearLine, clearScreenDown, cursorTo, moveCursor) as ANSI sequences on a stream.
// It follows Node's lib/internal/readline/interface.js for input that is not a terminal: lines end at \n, \r\n or a
// lone \r (a \r\n split between two chunks is one ending if the \n comes within crlfDelay ms), and the last line is
// emitted at the end of the input even with no newline. Differences (docs/docs/guides/node-compatibility.md):
//   - no line editing, history, completion or 'keypress' events: on a terminal the input is read in its normal
//     (cooked) mode, the terminal echoes and edits the line, and rl.line / rl.cursor stay empty;
//   - no 'SIGINT', 'SIGTSTP' or 'SIGCONT' events; emitKeypressEvents does nothing.
import { EventEmitter } from "xisto:events";
import type { Listener } from "xisto:events";
import { Readable, Writable, AbortError } from "xisto:stream";
import { StringDecoder } from "xisto:string_decoder";

/** The options of createInterface. */
export interface InterfaceOptions {
  input: Readable;
  output?: Writable | null;
  prompt?: string;
  crlfDelay?: number;
  terminal?: boolean;
  historySize?: int;
  removeHistoryDuplicates?: boolean;
  escapeCodeTimeout?: number;
  tabSize?: int;
}

class ReadlineError extends Error {
  code: string;
  constructor(code: string, message: string) {
    super(message);
    this.code = code;
  }
}

const kMinCrlfDelay: number = 100;

/** What for await reads: the lines not taken yet; the input pauses while 1024 wait. */
class LineQueue {
  lines: string[] = [];
  head: int = 0;
  done: boolean = false;
  wake: (() => void) | null = null;
  wakeup(): void {
    const w = this.wake;
    this.wake = null;
    if (w !== null) w();
  }
}

/** The readline Interface without question(): readline.Interface and readline.promises.Interface add theirs. */
export class InterfaceBase extends EventEmitter {
  input: Readable;
  output: Writable | null = null;
  terminal: boolean = false;
  crlfDelay: number = kMinCrlfDelay;
  historySize: int = 30;
  closed: boolean = false;
  paused: boolean = false;
  /** The line being typed (terminal line editing: not supported, always ""). */
  line: string = "";
  cursor: int = 0;
  history: string[] = [];
  private promptText: string = "> ";
  private oldPrompt: string = "";
  private lineBuffer: string = "";
  private sawReturnAt: number = 0;
  private decoder: StringDecoder = new StringDecoder("utf8");
  protected questionCallback: ((answer: string) => void) | null = null;
  private queue: LineQueue | null = null;
  private onData: Listener;
  private onEnd: Listener;
  private onError: Listener;

  constructor(input: Readable, output: Writable | null, terminal: boolean | null, options: InterfaceOptions | null) {
    super();
    this.input = input;
    this.output = output;
    if (options !== null) {
      const p = options.prompt;
      if (p !== null && p !== undefined) this.promptText = p;
      const d = options.crlfDelay;
      if (d !== null && d !== undefined && d > 0) this.crlfDelay = Math.max(kMinCrlfDelay, d);
      const h = options.historySize;
      if (h !== null && h !== undefined) this.historySize = h;
    }
    this.terminal = terminal !== null ? (terminal as boolean) : false;
    this.onData = (...a: any[]): void => {
      const chunk: any = a[0];
      if (typeof chunk === "string") this.normalWrite(chunk as string);
      else this.normalWrite(this.decoder.write(chunk as Uint8Array));
    };
    this.onEnd = (...a: any[]): void => {
      const rest = this.decoder.end();
      if (rest.length > 0) this.normalWrite(rest);
      if (this.lineBuffer.length > 0) {
        const last = this.lineBuffer;
        this.lineBuffer = "";
        this.onLine(last);
      }
      this.close();
    };
    this.onError = (...a: any[]): void => {
      this.emit("error", a[0]);
    };
    input.on("error", this.onError);
    input.on("data", this.onData);
    input.on("end", this.onEnd);
    input.resume();
  }

  // the chunk's lines (Node's kNormalWrite)
  private normalWrite(s0: string): void {
    let s = s0;
    if (this.sawReturnAt !== 0 && Date.now() - this.sawReturnAt <= this.crlfDelay) {
      if (s.charCodeAt(0) === 10) s = s.slice(1);
      this.sawReturnAt = 0;
    }
    let ending = false;
    for (let i = 0; i < s.length && !ending; i++) {
      const c = s.charCodeAt(i);
      if (c === 10 || c === 13) ending = true;
    }
    if (!ending) {
      if (s.length > 0) this.lineBuffer += s;
      return;
    }
    if (this.lineBuffer.length > 0) {
      s = this.lineBuffer + s;
      this.lineBuffer = "";
    }
    this.sawReturnAt = s.endsWith("\r") ? Date.now() : 0;
    const lines: string[] = [];
    let start = 0;
    for (let i = 0; i < s.length; i++) {
      const c = s.charCodeAt(i);
      if (c === 10) {
        lines.push(s.slice(start, i));
        start = i + 1;
      } else if (c === 13) {
        lines.push(s.slice(start, i));
        if (i + 1 < s.length && s.charCodeAt(i + 1) === 10) i++;
        start = i + 1;
      }
    }
    this.lineBuffer = s.slice(start);
    for (const line of lines) {
      this.onLine(line);
      if (this.closed) return;
    }
  }
  private onLine(line: string): void {
    const cb = this.questionCallback;
    if (cb !== null) {
      this.questionCallback = null;
      this.setPrompt(this.oldPrompt);
      cb(line);
    } else {
      this.emit("line", line);
    }
  }
  protected writeToOutput(s: string): void {
    const out = this.output;
    if (out !== null) out.write(s);
  }
  protected askQuestion(query: string, cb: (answer: string) => void): void {
    if (this.closed) throw new ReadlineError("ERR_USE_AFTER_CLOSE", "readline was closed");
    if (this.questionCallback !== null) {
      this.prompt();
      return;
    }
    this.oldPrompt = this.promptText;
    this.setPrompt(query);
    this.questionCallback = cb;
    this.prompt();
  }

  setPrompt(prompt: string): void {
    this.promptText = prompt;
  }
  getPrompt(): string {
    return this.promptText;
  }
  /** Writes the prompt to the output (and resumes the input). */
  prompt(preserveCursor: boolean = false): void {
    if (this.paused) this.resume();
    this.writeToOutput(this.promptText);
  }
  /** Data as if it came from the input. */
  write(data: string): void {
    if (this.closed) throw new ReadlineError("ERR_USE_AFTER_CLOSE", "readline was closed");
    if (this.paused) this.resume();
    this.normalWrite(data);
  }
  pause(): InterfaceBase {
    if (this.paused) return this;
    this.input.pause();
    this.paused = true;
    this.emit("pause");
    return this;
  }
  resume(): InterfaceBase {
    if (!this.paused) return this;
    this.input.resume();
    this.paused = false;
    this.emit("resume");
    return this;
  }
  /** Ends the interface: the input is paused and let go, 'close' is emitted. */
  close(): void {
    if (this.closed) return;
    this.pause();
    this.closed = true;
    this.input.off("data", this.onData);
    this.input.off("end", this.onEnd);
    this.input.off("error", this.onError);
    this.questionCallback = null;
    this.onClose();
    this.emit("close");
    const q = this.queue;
    if (q !== null) {
      q.done = true;
      q.wakeup();
    }
  }
  protected onClose(): void {}
  getCursorPos(): { rows: int; cols: int } {
    return { rows: 0, cols: this.cursor };
  }

  private lineQueue(): LineQueue {
    const existing = this.queue;
    if (existing !== null) return existing;
    const q = new LineQueue();
    this.queue = q;
    if (this.closed) q.done = true;
    this.on("line", (line: string): void => {
      q.lines.push(line);
      if (q.lines.length - q.head >= 1024) this.pause();
      q.wakeup();
    });
    return q;
  }
  /** for await (const line of rl): the lines until the interface closes (the end of the input). */
  async *[Symbol.asyncIterator](): AsyncGenerator<string> {
    const q = this.lineQueue();
    while (true) {
      if (q.head < q.lines.length) {
        const line = q.lines[q.head];
        q.head++;
        if (q.head === q.lines.length) {
          q.lines = [];
          q.head = 0;
          if (this.paused && !this.closed) this.resume();
        }
        yield line;
        continue;
      }
      if (q.done) return;
      await new Promise<void>((resolve: () => void): void => {
        q.wake = resolve;
      });
    }
  }
}

/** node:readline's Interface: question(query, cb). */
export class Interface extends InterfaceBase {
  /** Writes query and calls cb with the next line. */
  question(query: string, cb: (answer: string) => void): void {
    this.askQuestion(query, cb);
  }
}

/** readline.promises' Interface: question(query) gives a promise of the next line (rejected if it closes first). */
export class PromisesInterface extends InterfaceBase {
  private pendingReject: ((e: Error) => void) | null = null;
  question(query: string): Promise<string> {
    return new Promise<string>((resolve: (v: string) => void, reject: (e: Error) => void): void => {
      if (this.closed) {
        reject(new ReadlineError("ERR_USE_AFTER_CLOSE", "readline was closed"));
        return;
      }
      this.pendingReject = reject;
      this.askQuestion(query, (answer: string): void => {
        this.pendingReject = null;
        resolve(answer);
      });
    });
  }
  protected override onClose(): void {
    const r = this.pendingReject;
    this.pendingReject = null;
    if (r !== null) r(new AbortError("The operation was aborted"));
  }
}

function outputIsTTY(output: Writable | null): boolean {
  return false; // (no terminal mode: see the header)
}

/** createInterface({ input, output, prompt, crlfDelay, terminal }) or createInterface(input, output). */
export function createInterface(input: InterfaceOptions | Readable, output: Writable | null = null, completer: any = null, terminal: boolean | null = null): Interface {
  if (input instanceof Readable) return new Interface(input as Readable, output, terminal !== null ? terminal : outputIsTTY(output), null);
  const o = input as InterfaceOptions;
  const out = o.output !== undefined ? (o.output as Writable | null) : null;
  const t = o.terminal;
  return new Interface(o.input, out, t !== undefined && t !== null ? (t as boolean) : outputIsTTY(out), o);
}
/** readline.promises.createInterface: question() gives a promise. */
export function createPromisesInterface(input: InterfaceOptions | Readable, output: Writable | null = null, completer: any = null, terminal: boolean | null = null): PromisesInterface {
  if (input instanceof Readable) return new PromisesInterface(input as Readable, output, terminal !== null ? terminal : outputIsTTY(output), null);
  const o = input as InterfaceOptions;
  const out = o.output !== undefined ? (o.output as Writable | null) : null;
  const t = o.terminal;
  return new PromisesInterface(o.input, out, t !== undefined && t !== null ? (t as boolean) : outputIsTTY(out), o);
}

/** readline.promises (node:readline/promises is runtime/node/readline_promises.ts). */
export class promises {
  static createInterface(input: InterfaceOptions | Readable, output: Writable | null = null, completer: any = null, terminal: boolean | null = null): PromisesInterface {
    return createPromisesInterface(input, output, completer, terminal);
  }
}

// ---- cursor helpers: ANSI sequences written to a stream (cb: once written) ----

function writeSeq(stream: Writable | null, seq: string, cb: (() => void) | null): boolean {
  if (stream === null) {
    if (cb !== null) process.nextTick(cb as () => void);
    return true;
  }
  const s = stream as Writable;
  return s.write(seq, (e?: Error | null): void => {
    if (cb !== null) (cb as () => void)();
  });
}
/** Clears the line of the cursor: dir -1 to its left, 1 to its right, 0 all of it. */
export function clearLine(stream: Writable | null, dir: int, cb: (() => void) | null = null): boolean {
  const seq = dir < 0 ? "\x1b[1K" : dir > 0 ? "\x1b[0K" : "\x1b[2K";
  return writeSeq(stream, seq, cb);
}
/** Clears from the cursor to the end of the screen. */
export function clearScreenDown(stream: Writable | null, cb: (() => void) | null = null): boolean {
  return writeSeq(stream, "\x1b[0J", cb);
}
/** Moves the cursor to column x (and row y when given; -1: none). */
export function cursorTo(stream: Writable | null, x: int, y: int = -1, cb: (() => void) | null = null): boolean {
  const seq = y < 0 ? `\x1b[${x + 1}G` : `\x1b[${y + 1};${x + 1}H`;
  return writeSeq(stream, seq, cb);
}
/** Moves the cursor dx columns and dy rows from where it is. */
export function moveCursor(stream: Writable | null, dx: int, dy: int, cb: (() => void) | null = null): boolean {
  let seq = "";
  if (dx < 0) seq += `\x1b[${-dx}D`;
  else if (dx > 0) seq += `\x1b[${dx}C`;
  if (dy < 0) seq += `\x1b[${-dy}A`;
  else if (dy > 0) seq += `\x1b[${dy}B`;
  return writeSeq(stream, seq, cb);
}
/** Node starts 'keypress' events on the stream; xisto has none (no terminal line editing). */
export function emitKeypressEvents(stream: Readable, iface: InterfaceBase | null = null): void {}
