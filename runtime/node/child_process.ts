// xisto:child_process (node:child_process): spawn, exec, execFile (callbacks), spawnSync, execFileSync, execSync.
// spawn's stdout and stderr give 'data' as the child writes (tasks of the event loop read the pipes), then 'exit' and
// 'close' as in Node. Differences from Node (docs/docs/guides/node-compatibility.md): stdio is always three pipes
// (no 'inherit', no extra fds, no IPC/fork); the streams are EventEmitters ('data', 'end', setEncoding, write/end on
// stdin), not stream.Readable/Writable; execSync and execFileSync return text (Node: a Buffer unless an encoding is
// given); not on Windows yet.
import { EventEmitter } from "xisto:events";

declare function cp_spawn(file: string, args: string[], cwd: string, env: string[], useEnv: boolean): int[];
declare function cp_errno_name(e: int): string;
declare function cp_write(fd: int, bytes: Uint8Array): int;
declare function cp_wait(pid: int): int[];
declare function cp_kill(pid: int, sig: int): int;
declare function cp_run_sync(file: string, args: string[], cwd: string, input: string, outs: string[]): int[];
declare function net_read(fd: int): Uint8Array;
declare function net_close(fd: int): void;
declare function exec_sync(command: string): string;
declare function os_platform_name(): string;

const SIGNAMES_DARWIN: string[] = ["", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT", "SIGEMT", "SIGFPE", "SIGKILL",
  "SIGBUS", "SIGSEGV", "SIGSYS", "SIGPIPE", "SIGALRM", "SIGTERM", "SIGURG", "SIGSTOP", "SIGTSTP", "SIGCONT", "SIGCHLD", "SIGTTIN",
  "SIGTTOU", "SIGIO", "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGINFO", "SIGUSR1", "SIGUSR2"];
const SIGNAMES_LINUX: string[] = ["", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT", "SIGBUS", "SIGFPE", "SIGKILL",
  "SIGUSR1", "SIGSEGV", "SIGUSR2", "SIGPIPE", "SIGALRM", "SIGTERM", "SIGSTKFLT", "SIGCHLD", "SIGCONT", "SIGSTOP", "SIGTSTP", "SIGTTIN",
  "SIGTTOU", "SIGURG", "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO", "SIGPWR", "SIGSYS"];
function signames(): string[] {
  return os_platform_name() === "linux" ? SIGNAMES_LINUX : SIGNAMES_DARWIN;
}
function signalName(n: int): string | null {
  const t = signames();
  return n > 0 && n < t.length ? t[n] : null;
}
function signalNumber(name: string): int {
  const t = signames();
  for (let i = 1; i < t.length; i++) if (t[i] === name) return i;
  return 15;
}

/** The options of spawn() and the others. */
export interface SpawnOptions {
  cwd?: string;
  env?: Record<string, string>;
  shell?: boolean;
  input?: string;
  encoding?: string;
  /** Always "pipe" here (accepted for code written for Node). */
  stdio?: string;
}

/** A child's error: Node's code and message ("spawn nope ENOENT"). */
export class ChildError extends Error {
  code: string;
  errno: int;
  syscall: string;
  path: string;
  spawnargs: string[];
  constructor(code: string, errno: int, file: string, args: string[]) {
    super(`spawn ${file} ${code}`);
    this.code = code;
    this.errno = -errno;
    this.syscall = `spawn ${file}`;
    this.path = file;
    this.spawnargs = args;
  }
}

/** A command that ended with a failure (exec, execFile, execFileSync): Node's "Command failed: ..." error. */
export class ExecError extends Error {
  code: int | null;
  signal: string | null;
  killed: boolean = false;
  cmd: string;
  stdout: string = "";
  stderr: string = "";
  status: int | null;
  constructor(cmd: string, code: int | null, signal: string | null, stderr: string) {
    super(`Command failed: ${cmd}\n${stderr}`);
    this.cmd = cmd;
    this.code = code;
    this.status = code;
    this.signal = signal;
    this.stderr = stderr;
  }
}

/** One of a child's output pipes: 'data' (Buffers, or strings after setEncoding), then 'end' and 'close'. */
export class ChildOutput extends EventEmitter {
  private encoding: string = "";
  setEncoding(encoding: string = "utf8"): ChildOutput {
    this.encoding = encoding;
    return this;
  }
  /** (internal) reads the pipe to its end. */
  async _pump(fd: int): Promise<void> {
    for (;;) {
      const chunk = net_read(fd);
      if (chunk.length === 0) break;
      if (this.encoding.length > 0) this.emit("data", new TextDecoder().decode(chunk));
      else this.emit("data", Buffer.from(chunk));
    }
    net_close(fd);
    this.emit("end");
    this.emit("close");
  }
}

/** A child's input pipe: write() text or bytes, end() closes it. */
export class ChildInput extends EventEmitter {
  private fd: int;
  constructor(fd: int) {
    super();
    this.fd = fd;
  }
  write(data: string | Uint8Array): boolean {
    if (this.fd < 0) return false;
    const bytes: Uint8Array = typeof data === "string" ? new TextEncoder().encode(data as string) : (data as Uint8Array);
    if (cp_write(this.fd, bytes) < 0) this.emit("error", new Error("write EPIPE"));
    return true;
  }
  end(data: string | Uint8Array | null = null): void {
    if (data !== null) this.write(data);
    if (this.fd >= 0) net_close(this.fd);
    this.fd = -1;
    this.emit("finish");
  }
}

/** A running child: pid, stdin/stdout/stderr, kill(); 'spawn', 'exit' (code, signal), 'close', 'error'. */
export class ChildProcess extends EventEmitter {
  pid: int = 0;
  exitCode: int | null = null;
  signalCode: string | null = null;
  killed: boolean = false;
  spawnfile: string;
  spawnargs: string[];
  stdin: ChildInput = new ChildInput(-1);
  stdout: ChildOutput = new ChildOutput();
  stderr: ChildOutput = new ChildOutput();
  private task: Promise<void> | null = null;

  constructor(file: string, args: string[]) {
    super();
    this.spawnfile = file;
    this.spawnargs = withFile(file, args);
  }
  /** (internal) starts it. */
  _start(file: string, args: string[], options: SpawnOptions | null): void {
    const cwd = options !== null && options.cwd !== null ? (options.cwd as string) : "";
    const env: string[] = [];
    const given: Record<string, string> | null = options !== null && options.env !== null ? (options.env as Record<string, string>) : null;
    const useEnv = given !== null;
    if (given !== null) for (const k of Object.keys(given)) env.push(`${k}=${given[k] ?? ""}`);
    const r = cp_spawn(file, args, cwd, env, useEnv);
    if (r[0] < 0) {
      const err = new ChildError(cp_errno_name(-r[0]), -r[0], file, args);
      setImmediate(() => {
        this.emit("error", err);
        this.emit("close", -2, null);
      });
      return;
    }
    this.pid = r[0];
    this.stdin = new ChildInput(r[1]);
    this.task = this.watch(r[2], r[3]);
  }
  private async watch(outFd: int, errFd: int): Promise<void> {
    this.emit("spawn");
    const out = this.stdout._pump(outFd); // (both read at once, each on a task of its own)
    const err = this.stderr._pump(errFd);
    await out;
    await err;
    const st = cp_wait(this.pid);
    this.exitCode = st[1] > 0 ? null : st[0];
    this.signalCode = signalName(st[1]);
    this.emit("exit", this.exitCode, this.signalCode);
    this.emit("close", this.exitCode, this.signalCode);
  }
  /** Sends a signal (SIGTERM by default); true when it was sent. */
  kill(signal: string | int = "SIGTERM"): boolean {
    if (this.pid <= 0) return false;
    const n: int = typeof signal === "string" ? signalNumber(signal as string) : (signal as int);
    const ok = cp_kill(this.pid, n) === 0;
    if (ok) this.killed = true;
    return ok;
  }
}

function withFile(file: string, args: string[]): string[] {
  const out: string[] = [file];
  for (const a of args) out.push(a);
  return out;
}
function shellArgs(command: string): string[] {
  return ["-c", command];
}

/** Starts file with args; its output comes as events. */
export function spawn(file: string, args: string[] = [], options: SpawnOptions | null = null): ChildProcess {
  const shell = options !== null && options.shell === true;
  const realFile = shell ? "/bin/sh" : file;
  const realArgs = shell ? shellArgs(withFile(file, args).join(" ")) : args;
  const cp = new ChildProcess(file, args);
  cp._start(realFile, realArgs, options);
  return cp;
}

export type ExecCallback = (error: ExecError | null, stdout: string, stderr: string) => void;

function collect(cp: ChildProcess, cmd: string, callback: ExecCallback | null): ChildProcess {
  let out = "";
  let err = "";
  cp.stdout.setEncoding("utf8");
  cp.stderr.setEncoding("utf8");
  cp.stdout.on("data", (d: string) => {
    out += d;
  });
  cp.stderr.on("data", (d: string) => {
    err += d;
  });
  cp.on("error", (e: Error) => {
    if (callback !== null) {
      const x = new ExecError(cmd, null, null, "");
      x.message = e.message;
      callback(x, "", "");
    }
  });
  cp.on("close", (code: int | null, signal: string | null) => {
    if (callback === null || code === -2) return;
    if (code === 0) {
      callback(null, out, err);
      return;
    }
    const x = new ExecError(cmd, code, signal, err);
    x.stdout = out;
    x.killed = signal !== null;
    callback(x, out, err);
  });
  return cp;
}

/** Runs a shell command; the callback gets (error, stdout, stderr) when it ends. */
export function exec(command: string, callback: ExecCallback | null = null): ChildProcess {
  const cp = new ChildProcess("/bin/sh", shellArgs(command));
  cp._start("/bin/sh", shellArgs(command), null);
  return collect(cp, command, callback);
}

/** Runs file with args (no shell); the callback gets (error, stdout, stderr). */
export function execFile(file: string, args: string[] = [], callback: ExecCallback | null = null): ChildProcess {
  const cp = new ChildProcess(file, args);
  cp._start(file, args, null);
  return collect(cp, withFile(file, args).join(" "), callback);
}

/** What spawnSync() gives. */
export class SpawnSyncReturns {
  pid: int = 0;
  status: int | null = null;
  signal: string | null = null;
  stdout: string = "";
  stderr: string = "";
  output: (string | null)[] = [];
  error: Error | null = null;
}

/** Runs file with args to its end. */
export function spawnSync(file: string, args: string[] = [], options: SpawnOptions | null = null): SpawnSyncReturns {
  const cwd = options !== null && options.cwd !== null ? (options.cwd as string) : "";
  const input = options !== null && options.input !== null ? (options.input as string) : "";
  const shell = options !== null && options.shell === true;
  const outs: string[] = [];
  const r = shell
    ? cp_run_sync("/bin/sh", shellArgs(withFile(file, args).join(" ")), cwd, input, outs)
    : cp_run_sync(file, args, cwd, input, outs);
  const ret = new SpawnSyncReturns();
  if (r[0] < 0 && outs.length === 0) {
    ret.error = new ChildError(cp_errno_name(-r[0]), -r[0], file, args);
    return ret;
  }
  ret.status = r[1] > 0 ? null : r[0];
  ret.signal = signalName(r[1]);
  ret.stdout = outs[0];
  ret.stderr = outs[1];
  ret.output = [null, outs[0], outs[1]];
  return ret;
}

/** Runs file with args to its end and returns its output; a failure is thrown. */
export function execFileSync(file: string, args: string[] = [], options: SpawnOptions | null = null): string {
  const r = spawnSync(file, args, options);
  if (r.error !== null) throw r.error as Error;
  if (r.status !== 0) {
    const x = new ExecError(withFile(file, args).join(" "), r.status, r.signal, r.stderr);
    x.stdout = r.stdout;
    throw x;
  }
  return r.stdout;
}

/** Runs a shell command to its end and returns its output; a failure is thrown. */
export function execSync(command: string, options: SpawnOptions | null = null): string {
  if (options === null) return exec_sync(command);
  return execFileSync("/bin/sh", ["-c", command], options);
}
