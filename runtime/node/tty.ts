// xisto:tty (node:tty): isatty(fd), ReadStream and WriteStream, and process.stdin / process.stdout / process.stderr as
// streams (the compiler reads process.stdin as XistoStdio.stdin(), and process.stdout / process.stderr as streams
// when something other than .write(x) is done with them: src/checker.c; process.stdout.write(x) stays the built-in).
// Differences from Node (docs/docs/guides/node-compatibility.md):
//   - process.stdin is a tty.ReadStream whatever fd 0 is (Node: a net.Socket for a pipe, an fs.ReadStream for a file);
//     isTTY is true on a terminal and undefined otherwise, as in Node;
//   - ReadStream reads its descriptor in 64 KiB chunks on the event loop; no line editing, no 'keypress' events;
//   - process.stdout and process.stderr write synchronously (Node: also for files and terminals; pipes are async
//     there), and end() ends the stream object only (the descriptor stays open; console.log goes on);
//   - no 'resize' event (columns and rows are read when the stream is made; getWindowSize() reads them again).
import { Readable, Writable } from "xisto:stream";
import type { ErrorCallback } from "xisto:stream";

declare function tty_isatty(fd: int): boolean;
declare function tty_size(fd: int): int[];
declare function tty_set_raw(fd: int, raw: boolean): int;
declare function tty_write(fd: int, b: Uint8Array): void;
declare function tty_flush(fd: int): void;
declare function fd_read_start(fd: int, buf: Uint8Array, cb: (n: int) => void): void;
declare function fd_read_cancel(fd: int): boolean;
declare function fd_is_stream(fd: int): boolean;

/** true when fd is a terminal. */
export function isatty(fd: int): boolean {
  return tty_isatty(fd);
}

class TtyError extends Error {
  code: string;
  errno: int;
  syscall: string;
  constructor(code: string, errno: int, syscall: string) {
    super(`${syscall} ${code}`);
    this.code = code;
    this.errno = errno;
    this.syscall = syscall;
  }
}
const ERRNO_NAMES: Map<int, string> = new Map<int, string>([
  [5, "EIO"],
  [9, "EBADF"],
  [22, "EINVAL"],
  [25, "ENOTTY"],
]);
function errnoName(e: int): string {
  const n = ERRNO_NAMES.get(e);
  return n !== undefined ? n : "EIO";
}

/** A stream that reads a descriptor (process.stdin): Buffer chunks, or strings after setEncoding(), then 'end'. */
export class ReadStream extends Readable {
  fd: int;
  isTTY?: boolean;
  isRaw: boolean = false;
  bytesRead: int = 0;
  private inFlight: boolean = false; // a read of the descriptor is pending
  private wanted: boolean = false; // _read() asked for data and nothing came yet
  private userPaused: boolean = false; // pause() was called (and no resume() since): no read of the descriptor
  private buf: Uint8Array = new Uint8Array(0);
  constructor(fd: int) {
    super({ highWaterMark: 65536, emitClose: fd_is_stream(fd) }); // ('close' after 'end' on a pipe or a terminal, as Node's net.Socket; not on a file)
    this.fd = fd;
    if (tty_isatty(fd)) this.isTTY = true;
  }
  override _read(size: number): void {
    this.wanted = true;
    this.startRead();
  }
  private startRead(): void {
    if (this.inFlight || !this.wanted || this.userPaused || this.destroyed) return;
    this.inFlight = true;
    if (this.buf.length === 0) this.buf = Buffer.allocUnsafe(65536);
    fd_read_start(this.fd, this.buf, (n: int): void => this.onRead(n));
  }
  private onRead(n: int): void {
    this.inFlight = false;
    if (this.destroyed) return;
    this.wanted = false;
    if (n > 0) {
      this.bytesRead += n;
      const chunk = Buffer.from(this.buf.subarray(0, n));
      this.push(chunk);
    } else if (n === 0) {
      this.push(null);
    } else {
      this.destroy(new TtyError(errnoName(-n), -n, "read"));
    }
  }
  /** Stops the 'data' events; a pending read of the descriptor is let go, so it does not keep the program alive. */
  override pause(): Readable {
    super.pause();
    this.userPaused = true;
    if (this.inFlight && fd_read_cancel(this.fd)) this.inFlight = false;
    return this;
  }
  override resume(): Readable {
    super.resume();
    this.userPaused = false;
    this.startRead();
    return this;
  }
  override read(n: number | null = null): any {
    const r: any = super.read(n);
    this.startRead();
    return r;
  }
  override _destroy(err: Error | null, cb: ErrorCallback): void {
    if (this.inFlight && fd_read_cancel(this.fd)) this.inFlight = false;
    cb(err);
  }
  /** Raw mode on a terminal: each key as it is typed, no echo, Ctrl+C as a byte. */
  setRawMode(mode: boolean): ReadStream {
    if (this.isTTY !== true) throw new TtyError("ENOTTY", 25, "setRawMode");
    const e = tty_set_raw(this.fd, mode);
    if (e !== 0) throw new TtyError(errnoName(e), e, "setRawMode");
    this.isRaw = mode;
    return this;
  }
}

// Node's lib/internal/tty.js: the colours a terminal supports, from the environment
const COLORS_2: int = 1;
const COLORS_16: int = 4;
const COLORS_256: int = 8;
const COLORS_16m: int = 24;
const TERM_ENVS: Map<string, int> = new Map<string, int>([
  ["eterm", COLORS_16],
  ["cons25", COLORS_16],
  ["console", COLORS_16],
  ["cygwin", COLORS_16],
  ["dtterm", COLORS_16],
  ["gnome", COLORS_16],
  ["hurd", COLORS_16],
  ["jfbterm", COLORS_16],
  ["konsole", COLORS_16],
  ["kterm", COLORS_16],
  ["mlterm", COLORS_16],
  ["mosh", COLORS_16m],
  ["putty", COLORS_16],
  ["st", COLORS_16],
  ["rxvt-unicode-24bit", COLORS_16m],
  ["terminator", COLORS_16m],
  ["xterm-kitty", COLORS_16m],
]);
const CI_ENVS: string[] = ["APPVEYOR", "BUILDKITE", "CIRCLECI", "DRONE", "GITLAB_CI", "TRAVIS"];
function termMatches(t: string): boolean {
  if (t.includes("ansi") || t.includes("color") || t.includes("linux") || t.includes("direct")) return true;
  if (/^con[0-9]*x[0-9]/.test(t)) return true;
  return t.startsWith("rxvt") || t.startsWith("screen") || t.startsWith("xterm") || t.startsWith("vt100") || t.startsWith("vt220");
}
function envGet(env: Record<string, string>, k: string): string | null {
  return k in env ? env[k] : null;
}
/** The colour depth (bits: 1, 4, 8 or 24) a terminal with this environment supports. */
export function getColorDepth(env: Record<string, string>): int {
  const force = envGet(env, "FORCE_COLOR");
  if (force !== null) {
    if (force === "" || force === "1" || force === "true") return COLORS_16;
    if (force === "2") return COLORS_256;
    if (force === "3") return COLORS_16m;
    return COLORS_2;
  }
  if ("NODE_DISABLE_COLORS" in env || "NO_COLOR" in env || envGet(env, "TERM") === "dumb") return COLORS_2;
  const tmux = envGet(env, "TMUX");
  if (tmux !== null && tmux !== "") return COLORS_16m;
  const ci = envGet(env, "CI");
  if (ci !== null && ci !== "") {
    if ("GITHUB_ACTIONS" in env || "GITEA_ACTIONS" in env) return COLORS_16m;
    for (const s of CI_ENVS) if (s in env) return COLORS_256;
    return envGet(env, "CI_NAME") === "codeship" ? COLORS_256 : COLORS_2;
  }
  const tc = envGet(env, "TEAMCITY_VERSION");
  if (tc !== null) return /^(9\.(0*[1-9]\d*)\.|\d{2,}\.)/.test(tc) ? COLORS_16 : COLORS_2;
  const prog = envGet(env, "TERM_PROGRAM");
  if (prog === "iTerm.app") {
    const v = envGet(env, "TERM_PROGRAM_VERSION");
    return v === null || v === "" || /^[0-2]\./.test(v) ? COLORS_256 : COLORS_16m;
  }
  if (prog === "HyperTerm" || prog === "MacTerm") return COLORS_16m;
  if (prog === "Apple_Terminal") return COLORS_256;
  const ct = envGet(env, "COLORTERM");
  if (ct === "truecolor" || ct === "24bit") return COLORS_16m;
  const term = envGet(env, "TERM");
  if (term !== null && term !== "") {
    if (term.includes("truecolor")) return COLORS_16m;
    if (term.startsWith("xterm-256")) return COLORS_256;
    const t = term.toLowerCase();
    const known = TERM_ENVS.get(t);
    if (known !== undefined) return known;
    if (termMatches(t)) return COLORS_16;
  }
  if (ct !== null && ct !== "") return COLORS_16;
  return COLORS_2;
}

/** A stream that writes a descriptor: process.stdout (1) and process.stderr (2), through console.log's buffers. */
export class WriteStream extends Writable {
  fd: int;
  isTTY?: boolean;
  columns?: int;
  rows?: int;
  constructor(fd: int) {
    super({ decodeStrings: false, emitClose: false });
    this.fd = fd;
    if (tty_isatty(fd)) {
      this.isTTY = true;
      const s = tty_size(fd);
      if (s.length === 2) {
        this.columns = s[0];
        this.rows = s[1];
      }
    }
  }
  override _write(chunk: any, encoding: string, callback: ErrorCallback): void {
    if (typeof chunk === "string") {
      if (this.fd === 2) process.stderr.write(chunk as string);
      else process.stdout.write(chunk as string);
    } else {
      tty_write(this.fd, chunk as Uint8Array);
    }
    if (this.isTTY === true) tty_flush(this.fd);
    callback(null);
  }
  /** [columns, rows] of the terminal, read now. */
  getWindowSize(): int[] {
    const s = tty_size(this.fd);
    if (s.length === 2) {
      this.columns = s[0];
      this.rows = s[1];
    }
    return s;
  }
  /** The colour depth of the terminal (1, 4, 8 or 24 bits), from the environment (process.env by default). */
  getColorDepth(env: Record<string, string> | null = null): int {
    return getColorDepth(env !== null ? env : process.env);
  }
  /** true when the terminal shows at least count colours (16 by default). */
  hasColors(count: int = 16, env: Record<string, string> | null = null): boolean {
    return count <= Math.pow(2, this.getColorDepth(env));
  }
}

let stdinStream: ReadStream | null = null;
let stdoutStream: WriteStream | null = null;
let stderrStream: WriteStream | null = null;

/** process.stdin, process.stdout and process.stderr as streams, made on first use. */
export class XistoStdio {
  static stdin(): ReadStream {
    if (stdinStream === null) stdinStream = new ReadStream(0);
    return stdinStream as ReadStream;
  }
  static stdout(): WriteStream {
    if (stdoutStream === null) stdoutStream = new WriteStream(1);
    return stdoutStream as WriteStream;
  }
  static stderr(): WriteStream {
    if (stderrStream === null) stderrStream = new WriteStream(2);
    return stderrStream as WriteStream;
  }
}
