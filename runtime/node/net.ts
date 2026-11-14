// xisto:net (node:net): TCP sockets (createConnection/connect, Socket) and servers (createServer), isIP.
// A socket reads on a task of its own (a coroutine parked on the socket), so 'data' comes as bytes arrive, as in
// Node. Differences from Node (docs/docs/guides/node-compatibility.md): IPv4 listeners only; no Unix domain sockets
// (path); no pause()/resume() backpressure on reads; write() writes before it returns (its callback runs after);
// Socket is an EventEmitter, not yet a stream.Duplex (no pipe()).
import { EventEmitter } from "xisto:events";

declare function net_connect(host: string, port: int): int;
declare function net_code(code: int): string;
declare function net_read(fd: int): Uint8Array;
declare function net_read_error(): int;
declare function net_write(fd: int, bytes: Uint8Array): int;
declare function net_shutdown(fd: int, how: int): void;
declare function net_close(fd: int): void;
declare function net_listen(host: string, port: int): int;
declare function net_accept(lfd: int): int;
declare function net_address(fd: int, peer: boolean): string;
declare function net_port(fd: int, peer: boolean): int;
declare function net_wake(port: int): void;

/** A socket error, with Node's code and message. */
export class SystemError extends Error {
  code: string;
  syscall: string;
  errno: int;
  constructor(code: string, syscall: string, detail: string) {
    super(`${syscall} ${code}${detail}`);
    this.code = code;
    this.syscall = syscall;
    this.errno = 0;
  }
}

/** What address() gives. */
export class AddressInfo {
  address: string;
  family: string;
  port: int;
  constructor(address: string, family: string, port: int) {
    this.address = address;
    this.family = family;
    this.port = port;
  }
}

/** The options of connect(). */
export interface NetConnectOpts {
  port: int;
  host?: string;
}

/** A TCP connection: 'connect', 'data' (Buffers, or strings after setEncoding), 'end', 'close', 'error'. */
export class Socket extends EventEmitter {
  remoteAddress: string = "";
  remotePort: int = 0;
  localAddress: string = "";
  localPort: int = 0;
  remoteFamily: string = "";
  bytesRead: int = 0;
  bytesWritten: int = 0;
  connecting: boolean = false;
  destroyed: boolean = false;
  readyState: string = "closed";
  private fd: int = -1;
  private encoding: string = "";
  private pending: Uint8Array[] = [];
  private ending: boolean = false;
  private task: Promise<void> | null = null;
  private hadError: boolean = false;

  /** (internal) a socket on an accepted connection: its addresses now, its reading once the server's
   * 'connection' listeners have set theirs (_start). */
  _attach(fd: int): void {
    this.fd = fd;
    this.readyState = "open";
    this.fill();
  }
  /** (internal) starts reading. */
  _start(): void {
    this.task = this.pump();
  }
  private fill(): void {
    this.remoteAddress = net_address(this.fd, true);
    this.remotePort = net_port(this.fd, true);
    this.localAddress = net_address(this.fd, false);
    this.localPort = net_port(this.fd, false);
    this.remoteFamily = this.remoteAddress.includes(":") ? "IPv6" : "IPv4";
  }
  /** Connects to port on host (localhost by default); the listener runs on 'connect'. */
  connect(port: int, host: string = "localhost", listener: (() => void) | null = null): Socket {
    if (listener !== null) this.once("connect", listener);
    this.connecting = true;
    this.readyState = "opening";
    this.task = this.run(port, host);
    return this;
  }
  private async run(port: int, host: string): Promise<void> {
    const fd = net_connect(host, port);
    this.connecting = false;
    if (fd < 0) {
      const code = net_code(fd);
      this.fail(new SystemError(code, code === "ENOTFOUND" ? "getaddrinfo" : "connect", code === "ENOTFOUND" ? ` ${host}` : ` ${host === "localhost" ? "127.0.0.1" : host}:${port}`));
      return;
    }
    this.fd = fd;
    this.readyState = "open";
    this.fill();
    this.emit("connect");
    this.emit("ready");
    for (const chunk of this.pending) this.send(chunk);
    this.pending = [];
    if (this.ending) net_shutdown(this.fd, 1);
    await this.pump();
  }
  private async pump(): Promise<void> {
    for (;;) {
      const chunk = net_read(this.fd);
      if (chunk.length === 0) break;
      this.bytesRead += chunk.length;
      if (this.encoding.length > 0) this.emit("data", new TextDecoder().decode(chunk));
      else this.emit("data", Buffer.from(chunk));
    }
    const err = net_read_error();
    if (err !== 0 && !this.destroyed) {
      this.fail(new SystemError(net_code(err), "read", ""));
      return;
    }
    if (!this.destroyed) this.emit("end");
    this.finish();
  }
  private finish(): void {
    if (this.fd >= 0) net_close(this.fd);
    this.fd = -1;
    this.destroyed = true;
    this.readyState = "closed";
    this.emit("close", this.hadError);
  }
  private fail(err: SystemError): void {
    this.hadError = true;
    this.emit("error", err);
    this.finish();
  }
  private send(bytes: Uint8Array): void {
    const r = net_write(this.fd, bytes);
    if (r < 0) {
      this.hadError = true;
      this.emit("error", new SystemError(net_code(r), "write", ""));
      return;
    }
    this.bytesWritten += bytes.length;
  }
  /** 'data' gives strings ("utf8") instead of Buffers. */
  setEncoding(encoding: string = "utf8"): Socket {
    this.encoding = encoding;
    return this;
  }
  /** Writes text or bytes; the callback runs once they are written. */
  write(data: string | Uint8Array, callback: (() => void) | null = null): boolean {
    const bytes: Uint8Array = typeof data === "string" ? new TextEncoder().encode(data as string) : (data as Uint8Array);
    if (this.fd < 0) this.pending.push(bytes);
    else this.send(bytes);
    if (callback !== null) setImmediate(callback);
    return true;
  }
  /** Writes the last data and closes this side (the peer gets 'end'). */
  end(data: string | Uint8Array | null = null, callback: (() => void) | null = null): Socket {
    if (data !== null) this.write(data);
    if (callback !== null) this.once("finish", callback);
    this.ending = true;
    if (this.fd >= 0) net_shutdown(this.fd, 1);
    setImmediate(() => {
      this.emit("finish");
    });
    return this;
  }
  /** Closes both sides at once. */
  destroy(err: Error | null = null): Socket {
    if (this.destroyed) return this;
    this.destroyed = true;
    if (err !== null) {
      this.hadError = true;
      this.emit("error", err);
    }
    if (this.fd >= 0) net_shutdown(this.fd, 2); // (the reading task wakes and closes it)
    return this;
  }
  setNoDelay(noDelay: boolean = true): Socket {
    return this; // (always on)
  }
  setKeepAlive(enable: boolean = false, initialDelay: int = 0): Socket {
    return this;
  }
  setTimeout(ms: int, callback: (() => void) | null = null): Socket {
    if (callback !== null) this.once("timeout", callback);
    return this;
  }
  ref(): Socket {
    return this;
  }
  unref(): Socket {
    return this;
  }
  address(): AddressInfo {
    return new AddressInfo(this.localAddress, this.localAddress.includes(":") ? "IPv6" : "IPv4", this.localPort);
  }
}

/** A connected socket (the listener runs on 'connect'). */
export function createConnection(port: int | NetConnectOpts, host: string | null = null, listener: (() => void) | null = null): Socket {
  const s = new Socket();
  if (typeof port === "number") return s.connect(port as int, host ?? "localhost", listener);
  const o = port as NetConnectOpts;
  return s.connect(o.port, o.host ?? "localhost", listener);
}
/** The same as createConnection(). */
export function connect(port: int | NetConnectOpts, host: string | null = null, listener: (() => void) | null = null): Socket {
  return createConnection(port, host, listener);
}

export type ConnectionListener = (socket: Socket) => void;

/** A TCP server (Node's net.Server; the name Server is xisto's built-in HTTP server): 'connection' (socket) for each client, 'listening', 'close', 'error'. */
export class NetServer extends EventEmitter {
  listening: boolean = false;
  private lfd: int = -1;
  private port: int = 0;
  private closing: boolean = false;
  private open: int = 0;
  private task: Promise<void> | null = null;

  constructor(listener: ConnectionListener | null = null) {
    super();
    if (listener !== null) this.on("connection", listener);
  }
  /** Listens on the port (0: any free one), on every address or the host given; the callback runs on 'listening'. */
  listen(port: int = 0, host: string = "", callback: (() => void) | null = null): NetServer {
    if (callback !== null) this.once("listening", callback);
    const fd = net_listen(host, port);
    if (fd < 0) {
      const code = net_code(fd);
      setImmediate(() => {
        this.emit("error", new SystemError(code, "listen", ` ${host.length > 0 ? host : "::"}:${port}`));
      });
      return this;
    }
    this.lfd = fd;
    this.port = net_port(fd, false);
    this.listening = true;
    setImmediate(() => {
      this.emit("listening");
    });
    this.task = this.acceptAll();
    return this;
  }
  private async acceptAll(): Promise<void> {
    for (;;) {
      const fd = net_accept(this.lfd);
      if (this.closing) {
        if (fd >= 0) net_close(fd);
        break;
      }
      if (fd < 0) continue;
      const s = new Socket();
      this.open++;
      s.on("close", () => {
        this.open--;
        this.maybeClosed();
      });
      s._attach(fd);
      this.emit("connection", s);
      s._start();
    }
    net_close(this.lfd);
    this.lfd = -1;
    this.maybeClosed();
  }
  private maybeClosed(): void {
    if (this.closing && this.lfd < 0 && this.open === 0 && this.listening) {
      this.listening = false;
      this.emit("close");
    }
  }
  /** Stops accepting; 'close' (and the callback) once the open connections end. */
  close(callback: (() => void) | null = null): NetServer {
    if (callback !== null) this.once("close", callback);
    if (this.closing || this.lfd < 0) return this;
    this.closing = true;
    net_wake(this.port); // (the accepting task wakes, sees closing and stops)
    return this;
  }
  address(): AddressInfo | null {
    return this.lfd < 0 && !this.listening ? null : new AddressInfo("0.0.0.0", "IPv4", this.port);
  }
  /** The number of open connections, to the callback. */
  getConnections(callback: (err: Error | null, count: int) => void): NetServer {
    const n = this.open;
    setImmediate(() => {
      callback(null, n);
    });
    return this;
  }
}

/** A server that calls the listener for each connection. */
export function createServer(listener: ConnectionListener | null = null): NetServer {
  return new NetServer(listener);
}

function isV4(s: string): boolean {
  const parts = s.split(".");
  if (parts.length !== 4) return false;
  for (const p of parts) {
    if (p.length === 0 || p.length > 3) return false;
    for (let i = 0; i < p.length; i++) {
      const c = p.charCodeAt(i);
      if (c < 48 || c > 57) return false;
    }
    if (p.length > 1 && p.startsWith("0")) return false;
    if (Number(p) > 255) return false;
  }
  return true;
}
function isV6(s: string): boolean {
  if (!s.includes(":")) return false;
  let rest = s;
  const pct = rest.indexOf("%");
  if (pct >= 0) rest = rest.slice(0, pct);
  const dbl = rest.indexOf("::");
  if (dbl >= 0 && rest.indexOf("::", dbl + 1) >= 0) return false;
  const groups = rest.split(":");
  let n = 0;
  for (let i = 0; i < groups.length; i++) {
    const g = groups[i];
    if (g.length === 0) continue;
    if (i === groups.length - 1 && g.includes(".")) {
      if (!isV4(g)) return false;
      n += 2;
      continue;
    }
    if (g.length > 4) return false;
    for (let k = 0; k < g.length; k++) {
      const c = g.charCodeAt(k);
      const hex = (c >= 48 && c <= 57) || (c >= 97 && c <= 102) || (c >= 65 && c <= 70);
      if (!hex) return false;
    }
    n++;
  }
  return dbl >= 0 ? n < 8 : n === 8;
}
/** 4 for an IPv4 address, 6 for IPv6, 0 otherwise. */
export function isIP(input: string): int {
  return isV4(input) ? 4 : isV6(input) ? 6 : 0;
}
export function isIPv4(input: string): boolean {
  return isV4(input);
}
export function isIPv6(input: string): boolean {
  return isV6(input);
}
