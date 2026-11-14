// xisto:process: the members of Node's process beyond argv, env, pid, cwd(), exit(), exitCode and
// stdout/stderr.write (those are built into the compiler). The compiler reads process.X for any other X as
// XistoProcess.X (src/checker.c), and loads this module when the program names one (src/parser.c).
// Differences from Node (docs/docs/guides/node-compatibility.md): 'uncaughtException' and 'unhandledRejection'
// listeners are not called (an uncaught error ends the program as before); memoryUsage().heapUsed is an estimate
// from the number of live objects; process.version is the Node version whose API xisto follows.
import { EventEmitter, Listener } from "xisto:events";

declare function os_platform_name(): string;
declare function os_arch_name(): string;
declare function process_hrtime(): int[];
declare function process_uptime(): number;
declare function process_rss(): number;
declare function process_heap_used(): number;
declare function process_cpu_times(): number[];
declare function process_rusage(): number[];
declare function process_ppid(): int;
declare function process_exec_path(): string;
declare function process_title(): string;
declare function process_chdir(dir: string): string;
declare function process_kill(pid: int, sig: int): int;
declare function process_umask(mask: int): int;
declare function process_ids(which: int): int;
declare function process_get_exit_code(): int;
declare function process_on_exit(cb: (code: int, before: boolean) => void): void;
declare function process_signal_watch(sig: int, cb: (sig: int) => void): void;
declare function process_signal_unwatch(sig: int): void;
declare function next_tick(cb: () => void): void;

/** What process.memoryUsage() gives, in bytes. */
export class MemoryUsage {
  rss: number;
  heapTotal: number;
  heapUsed: number;
  external: number;
  arrayBuffers: number;
  constructor(rss: number, heapTotal: number, heapUsed: number, external: number, arrayBuffers: number) {
    this.rss = rss;
    this.heapTotal = heapTotal;
    this.heapUsed = heapUsed;
    this.external = external;
    this.arrayBuffers = arrayBuffers;
  }
}

/** What process.cpuUsage() gives, in microseconds. */
export class CpuUsage {
  user: number;
  system: number;
  constructor(user: number, system: number) {
    this.user = user;
    this.system = system;
  }
}

/** What process.resourceUsage() gives (the fields xisto measures; the others are 0). */
export class ResourceUsage {
  userCPUTime: number = 0;
  systemCPUTime: number = 0;
  maxRSS: number = 0;
  sharedMemorySize: number = 0;
  unsharedDataSize: number = 0;
  unsharedStackSize: number = 0;
  minorPageFault: number = 0;
  majorPageFault: number = 0;
  swappedOut: number = 0;
  fsRead: number = 0;
  fsWrite: number = 0;
  ipcSent: number = 0;
  ipcReceived: number = 0;
  signalsCount: number = 0;
  voluntaryContextSwitches: number = 0;
  involuntaryContextSwitches: number = 0;
}

const SIGNALS: string[] = [
  "", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT", "SIGEMT", "SIGFPE", "SIGKILL", "SIGBUS", "SIGSEGV",
  "SIGSYS", "SIGPIPE", "SIGALRM", "SIGTERM", "SIGURG", "SIGSTOP", "SIGTSTP", "SIGCONT", "SIGCHLD", "SIGTTIN", "SIGTTOU",
  "SIGIO", "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGINFO", "SIGUSR1", "SIGUSR2",
];
const LINUX_SIGNALS: string[] = [
  "", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT", "SIGBUS", "SIGFPE", "SIGKILL", "SIGUSR1", "SIGSEGV",
  "SIGUSR2", "SIGPIPE", "SIGALRM", "SIGTERM", "SIGSTKFLT", "SIGCHLD", "SIGCONT", "SIGSTOP", "SIGTSTP", "SIGTTIN", "SIGTTOU",
  "SIGURG", "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO", "SIGPWR", "SIGSYS",
];

function signalTable(): string[] {
  return os_platform_name() === "linux" ? LINUX_SIGNALS : SIGNALS;
}
function signalNumber(name: string): int {
  const t = signalTable();
  for (let i = 1; i < t.length; i++) if (t[i] === name) return i;
  return -1;
}
function isSignal(name: string): boolean {
  return name.startsWith("SIG") && signalNumber(name) > 0;
}

function onSignal(sig: int): void {
  const t = signalTable();
  if (sig > 0 && sig < t.length) XistoProcess.emitter.emit(t[sig], t[sig], sig);
}
function onExit(code: int, before: boolean): void {
  XistoProcess.emitter.emit(before ? "beforeExit" : "exit", code);
}

let exitHooked = false;

function systemError(code: string, syscall: string, detail: string): Error {
  const texts: Map<string, string> = new Map<string, string>([
    ["ENOENT", "no such file or directory"],
    ["ENOTDIR", "not a directory"],
    ["EACCES", "permission denied"],
    ["ESRCH", "no such process"],
    ["EPERM", "operation not permitted"],
  ]);
  const text = texts.has(code) ? texts.get(code) : "i/o error";
  return new Error(`${code}: ${text}, ${syscall}${detail}`);
}

/** Node's process, as static members (the compiler reads process.X as XistoProcess.X). */
export class XistoProcess {
  static readonly emitter: EventEmitter = new EventEmitter();
  static readonly platform: string = os_platform_name();
  static readonly arch: string = os_arch_name();
  static readonly version: string = "v26.0.0";
  static readonly versions: Record<string, string> = { node: "26.0.0", xisto: "1" };
  static readonly release: Record<string, string> = { name: "node" };
  static readonly execPath: string = process_exec_path();
  static readonly execArgv: string[] = [];
  static readonly argv0: string = process_title();
  static title: string = process_title();
  static readonly ppid: int = process_ppid();
  static readonly allowedNodeEnvironmentFlags: Set<string> = new Set<string>();
  static debugPort: int = 9229;

  /** [seconds, nanoseconds] of a monotonic clock; given an earlier result, the time since it. */
  static hrtime(prev: int[] | null = null): int[] {
    const t = process_hrtime();
    if (prev === null) return t;
    let s = t[0] - prev[0];
    let ns = t[1] - prev[1];
    if (ns < 0) {
      s--;
      ns += 1000000000;
    }
    return [s, ns];
  }
  /** process.hrtime.bigint(): nanoseconds of a monotonic clock. */
  static hrtimeBigint(): bigint {
    const t = process_hrtime();
    return BigInt(t[0]) * 1000000000n + BigInt(t[1]);
  }
  /** Seconds since the program started. */
  static uptime(): number {
    return process_uptime();
  }
  static memoryUsage(): MemoryUsage {
    const rss = process_rss();
    const used = process_heap_used();
    return new MemoryUsage(rss, Math.max(rss, used), used, 0, 0);
  }
  /** process.memoryUsage.rss() */
  static memoryUsageRss(): number {
    return process_rss();
  }
  /** CPU time used, in microseconds; given an earlier result, the time since it. */
  static cpuUsage(prev: CpuUsage | null = null): CpuUsage {
    const t = process_cpu_times();
    if (prev === null) return new CpuUsage(t[0], t[1]);
    return new CpuUsage(t[0] - prev.user, t[1] - prev.system);
  }
  static resourceUsage(): ResourceUsage {
    const t = process_cpu_times();
    const r = process_rusage();
    const u = new ResourceUsage();
    u.userCPUTime = t[0];
    u.systemCPUTime = t[1];
    u.maxRSS = r[0];
    u.minorPageFault = r[1];
    u.majorPageFault = r[2];
    u.voluntaryContextSwitches = r[3];
    u.involuntaryContextSwitches = r[4];
    return u;
  }
  /** Calls fn with the arguments after the current work, before the promise reactions. */
  static nextTick(fn: Listener, ...args: any[]): void {
    next_tick((): void => {
      fn(...args);
    });
  }
  static chdir(dir: string): void {
    const err = process_chdir(dir);
    if (err.length > 0) throw systemError(err, "chdir", ` ${process.cwd()} -> '${dir}'`);
  }
  /** Sends a signal (a name or a number; SIGTERM by default) to a process. */
  static kill(pid: int, signal: string | int = "SIGTERM"): boolean {
    const sig: int = typeof signal === "string" ? signalNumber(signal as string) : (signal as int);
    if (sig < 0) throw new TypeError(`The value "${signal}" is invalid for option "signal"`);
    const err = process_kill(pid, sig);
    if (err !== 0) throw systemError(err === 1 ? "EPERM" : "ESRCH", "kill", "");
    return true;
  }
  static umask(mask: int = -1): int {
    return process_umask(mask);
  }
  static getuid(): int {
    return process_ids(0);
  }
  static getgid(): int {
    return process_ids(1);
  }
  static geteuid(): int {
    return process_ids(2);
  }
  static getegid(): int {
    return process_ids(3);
  }
  static getExitCode(): int {
    return process_get_exit_code();
  }
  static abort(): void {
    XistoProcess.kill(process.pid, "SIGABRT");
  }

  /** Prints a warning on stderr as Node does, and emits 'warning'. */
  static emitWarning(warning: string | Error, type: string = "Warning", code: string = ""): void {
    const text = typeof warning === "string" ? (warning as string) : (warning as Error).message;
    const name = typeof warning === "string" ? type : (warning as Error).name;
    if (XistoProcess.emitter.listenerCount("warning") > 0) XistoProcess.emitter.emit("warning", new Error(text), name);
    const c = code.length > 0 ? `[${code}] ` : "";
    console.error(`(node:${process.pid}) ${c}${name}: ${text}\n(Use \`node --trace-warnings ...\` to show where the warning was created)`);
  }

  private static hook(event: string): void {
    if ((event === "exit" || event === "beforeExit") && !exitHooked) {
      exitHooked = true;
      process_on_exit(onExit);
    } else if (isSignal(event)) {
      process_signal_watch(signalNumber(event), onSignal);
    }
  }
  private static unhook(event: string): void {
    if (isSignal(event) && XistoProcess.emitter.listenerCount(event) === 0) process_signal_unwatch(signalNumber(event));
  }

  static on(event: string, listener: Listener): EventEmitter {
    XistoProcess.hook(event);
    return XistoProcess.emitter.on(event, listener);
  }
  static addListener(event: string, listener: Listener): EventEmitter {
    return XistoProcess.on(event, listener);
  }
  static once(event: string, listener: Listener): EventEmitter {
    XistoProcess.hook(event);
    return XistoProcess.emitter.once(event, listener);
  }
  static prependListener(event: string, listener: Listener): EventEmitter {
    XistoProcess.hook(event);
    return XistoProcess.emitter.prependListener(event, listener);
  }
  static off(event: string, listener: Listener): EventEmitter {
    XistoProcess.emitter.off(event, listener);
    XistoProcess.unhook(event);
    return XistoProcess.emitter;
  }
  static removeListener(event: string, listener: Listener): EventEmitter {
    return XistoProcess.off(event, listener);
  }
  static removeAllListeners(event: string | null = null): EventEmitter {
    XistoProcess.emitter.removeAllListeners(event);
    if (event !== null) XistoProcess.unhook(event);
    return XistoProcess.emitter;
  }
  static emit(event: string, ...args: any[]): boolean {
    return XistoProcess.emitter.emit(event, ...args);
  }
  static listenerCount(event: string): int {
    return XistoProcess.emitter.listenerCount(event);
  }
  static listeners(event: string): Listener[] {
    return XistoProcess.emitter.listeners(event);
  }
  static eventNames(): string[] {
    return XistoProcess.emitter.eventNames();
  }
  static setMaxListeners(n: int): EventEmitter {
    return XistoProcess.emitter.setMaxListeners(n);
  }
}
