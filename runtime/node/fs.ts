// xisto:fs (node:fs): Node's synchronous file system API, written in xisto over system calls of the runtime
// (runtime/xisto_rt.h, "node:fs"). readFileSync, existsSync and the file descriptor calls are the runtime's own
// (xisto:fs_native: the checker picks the text or the bytes flavour of readFileSync by its arguments). The callback
// API (fs.readFile & co, runtime/node/fs_callbacks.ts), fs.promises (runtime/node/fs_promises.ts) and the streams
// (runtime/node/stream.ts) are loaded only by the programs that name them (src/parser.c, fs_extras).
// Errors are Node's: "ENOENT: no such file or directory, open '/x'", with err.code, errno, syscall and path (dest
// for the calls on two paths). Paths are strings (not Buffers or URLs: fileURLToPath(url) gives one).
export { readFileSync, existsSync, openSync, closeSync, readSync } from "xisto:fs_native";
import { existsSync as __existsSync } from "xisto:fs_native";
import { join as __pjoin, resolve as __presolve, dirname as __pdirname, basename as __pbasename } from "xisto:path";

declare function fs_throw(e: int, syscall: string, path: string, dest: string): void;
declare function fs_last_err(): int;
declare function fs_errcode_of(e: int): string;
declare function fs_mkdir(path: string, mode: int): int;
declare function fs_rmdir(path: string): int;
declare function fs_unlink_rc(path: string): int;
declare function fs_rename(from: string, to: string): int;
declare function fs_chmod(path: string, mode: int): int;
declare function fs_access(path: string, mode: int): int;
declare function fs_truncate(path: string, len: int): int;
declare function fs_symlink(target: string, path: string): int;
declare function fs_link(existing: string, path: string): int;
declare function fs_utimes(path: string, atime: number, mtime: number, follow: boolean): int;
declare function fs_stat(path: string, follow: boolean, fd: int): number[];
declare function fs_readdir(path: string): string[];
declare function fs_realpath(path: string): string;
declare function fs_readlink(path: string): string;
declare function fs_mkdtemp(prefix: string): string;
declare function fs_write_str(path: string, data: string, flags: string, mode: int): int;
declare function fs_write_buf(path: string, data: Uint8Array, flags: string, mode: int): int;
declare function fs_copy_file(src: string, dest: string, excl: boolean): int;
declare function fs_date_ms(d: Date): number;
declare function fs_open_fd(path: string, flags: string, mode: int): int;
declare function fs_close_fd(fd: int): int;
declare function fs_read_fd(fd: int, b: Uint8Array, off: int, len: int, pos: int): int;
declare function fs_write_fd(fd: int, b: Uint8Array, off: int, len: int, pos: int): int;

// ---- errors ----

/** Throws Node's error for the -errno r of syscall (r < 0). */
export function __fsCheck(r: int, syscall: string, path: string, dest: string = ""): void {
  if (r < 0) fs_throw(r, syscall, path, dest);
}
/** The code ("ENOENT") of a -errno. */
export function __fsCode(r: int): string {
  return fs_errcode_of(-r);
}
// Node's errors that are not a system call's: a code and a message (ERR_FS_EISDIR & co)
class FsCodeError extends Error {
  code: string;
  errno?: int;
  syscall?: string;
  path?: string;
  constructor(name: string, code: string, message: string, errno: int, syscall: string, path: string) {
    super(message);
    this.name = name;
    this.code = code;
    if (syscall !== "") {
      this.errno = errno;
      this.syscall = syscall;
      this.path = path;
    }
  }
}

// ---- fs.constants ----

/** fs.constants: the file access, copy and type constants (the open flags as macOS and Linux number them). */
export class constants {
  static readonly F_OK: int = 0;
  static readonly R_OK: int = 4;
  static readonly W_OK: int = 2;
  static readonly X_OK: int = 1;
  static readonly COPYFILE_EXCL: int = 1;
  static readonly COPYFILE_FICLONE: int = 2;
  static readonly COPYFILE_FICLONE_FORCE: int = 4;
  static readonly S_IFMT: int = 0o170000;
  static readonly S_IFREG: int = 0o100000;
  static readonly S_IFDIR: int = 0o040000;
  static readonly S_IFCHR: int = 0o020000;
  static readonly S_IFBLK: int = 0o060000;
  static readonly S_IFIFO: int = 0o010000;
  static readonly S_IFLNK: int = 0o120000;
  static readonly S_IFSOCK: int = 0o140000;
  static readonly S_IRWXU: int = 0o700;
  static readonly S_IRUSR: int = 0o400;
  static readonly S_IWUSR: int = 0o200;
  static readonly S_IXUSR: int = 0o100;
  static readonly S_IRWXG: int = 0o70;
  static readonly S_IRGRP: int = 0o40;
  static readonly S_IWGRP: int = 0o20;
  static readonly S_IXGRP: int = 0o10;
  static readonly S_IRWXO: int = 0o7;
  static readonly S_IROTH: int = 0o4;
  static readonly S_IWOTH: int = 0o2;
  static readonly S_IXOTH: int = 0o1;
  static readonly O_RDONLY: int = 0;
  static readonly O_WRONLY: int = 1;
  static readonly O_RDWR: int = 2;
  static readonly UV_DIRENT_UNKNOWN: int = 0;
  static readonly UV_DIRENT_FILE: int = 1;
  static readonly UV_DIRENT_DIR: int = 2;
  static readonly UV_DIRENT_LINK: int = 3;
  static readonly UV_DIRENT_FIFO: int = 4;
  static readonly UV_DIRENT_SOCKET: int = 5;
  static readonly UV_DIRENT_CHAR: int = 6;
  static readonly UV_DIRENT_BLOCK: int = 7;
}
/** F_OK & co, also at the top of the module (fs.F_OK, deprecated in Node but there). */
export const F_OK: int = 0;
export const R_OK: int = 4;
export const W_OK: int = 2;
export const X_OK: int = 1;

// ---- Stats and Dirent ----

/** What statSync, lstatSync and fstatSync give. */
export class Stats {
  dev: int = 0;
  mode: int = 0;
  nlink: int = 0;
  uid: int = 0;
  gid: int = 0;
  rdev: int = 0;
  blksize: int = 0;
  ino: number = 0; // (a number, as Node's: a pipe's inode on macOS is above 2^53, which no int holds)
  size: int = 0;
  blocks: int = 0;
  atimeMs: number = 0;
  mtimeMs: number = 0;
  ctimeMs: number = 0;
  birthtimeMs: number = 0;
  constructor(v: number[]) {
    if (v.length < 14) return;
    this.dev = Math.trunc(v[0]);
    this.mode = Math.trunc(v[1]);
    this.nlink = Math.trunc(v[2]);
    this.uid = Math.trunc(v[3]);
    this.gid = Math.trunc(v[4]);
    this.rdev = Math.trunc(v[5]);
    this.blksize = Math.trunc(v[6]);
    this.ino = v[7];
    this.size = Math.trunc(v[8]);
    this.blocks = Math.trunc(v[9]);
    this.atimeMs = v[10];
    this.mtimeMs = v[11];
    this.ctimeMs = v[12];
    this.birthtimeMs = v[13];
  }
  get atime(): Date {
    return new Date(Math.trunc(this.atimeMs));
  }
  get mtime(): Date {
    return new Date(Math.trunc(this.mtimeMs));
  }
  get ctime(): Date {
    return new Date(Math.trunc(this.ctimeMs));
  }
  get birthtime(): Date {
    return new Date(Math.trunc(this.birthtimeMs));
  }
  isFile(): boolean {
    return (this.mode & 0o170000) === 0o100000;
  }
  isDirectory(): boolean {
    return (this.mode & 0o170000) === 0o040000;
  }
  isSymbolicLink(): boolean {
    return (this.mode & 0o170000) === 0o120000;
  }
  isBlockDevice(): boolean {
    return (this.mode & 0o170000) === 0o060000;
  }
  isCharacterDevice(): boolean {
    return (this.mode & 0o170000) === 0o020000;
  }
  isFIFO(): boolean {
    return (this.mode & 0o170000) === 0o010000;
  }
  isSocket(): boolean {
    return (this.mode & 0o170000) === 0o140000;
  }
}

/** A directory entry: readdirSync(path, { withFileTypes: true }). */
export class Dirent {
  name: string;
  parentPath: string;
  private kind: string; // the runtime's letter: f d l b c p s u
  constructor(name: string, parentPath: string, kind: string) {
    this.name = name;
    this.parentPath = parentPath;
    this.kind = kind;
  }
  isFile(): boolean {
    return this.kind === "f";
  }
  isDirectory(): boolean {
    return this.kind === "d";
  }
  isSymbolicLink(): boolean {
    return this.kind === "l";
  }
  isBlockDevice(): boolean {
    return this.kind === "b";
  }
  isCharacterDevice(): boolean {
    return this.kind === "c";
  }
  isFIFO(): boolean {
    return this.kind === "p";
  }
  isSocket(): boolean {
    return this.kind === "s";
  }
}

// ---- options ----

export interface MkdirOptions {
  recursive?: boolean;
  mode?: int;
}
export interface RmOptions {
  recursive?: boolean;
  force?: boolean;
  maxRetries?: int;
  retryDelay?: int;
}
export interface RmdirOptions {
  recursive?: boolean;
  maxRetries?: int;
  retryDelay?: int;
}
export interface ReaddirOptions {
  withFileTypes?: boolean;
  recursive?: boolean;
  encoding?: string | null;
}
export interface StatOptions {
  throwIfNoEntry?: boolean;
  bigint?: boolean;
}
export interface WriteFileOptions {
  encoding?: string | null;
  mode?: int;
  flag?: string;
  flush?: boolean;
}
export interface CopyOptions {
  recursive?: boolean;
  force?: boolean;
  errorOnExist?: boolean;
  dereference?: boolean;
  preserveTimestamps?: boolean;
  verbatimSymlinks?: boolean;
  filter?: (source: string, destination: string) => boolean;
}

// ---- files ----

function writeAll(path: string, data: string | Uint8Array, options: string | WriteFileOptions | null, flag: string, other: boolean): void {
  let enc = "utf8";
  let mode: int = 0o666;
  let fl = flag;
  if (typeof options === "string") {
    enc = options;
  } else if (options !== null) {
    enc = options.encoding ?? "utf8";
    mode = options.mode ?? 0o666;
    fl = options.flag ?? flag;
  }
  let r: int = 0;
  if (typeof data !== "string") {
    r = fs_write_buf(path, data, fl, mode);
  } else if (enc === "utf8" || enc === "utf-8") {
    r = fs_write_str(path, data, fl, mode);
  } else if (other) {
    r = fs_write_buf(path, Buffer.alloc(Buffer.byteLength(data, enc), data, enc), fl, mode);
  } else {
    throw new Error(`xisto: text in the encoding '${enc}' is written when the encoding is written where the call is: writeFileSync(path, text, "${enc}")`);
  }
  __fsCheck(r, "open", path);
}

/** Writes data (text, UTF-8 unless an encoding says otherwise, or bytes) to a file, replacing it. */
export function writeFileSync(path: string, data: string | Uint8Array, options?: string | WriteFileOptions): void {
  writeAll(path, data, options, "w", false);
}
/** (the checker's choice for an encoding written in the call: "utf8" here, another one in __encoding) */
export function writeFileSync__utf8(path: string, data: string | Uint8Array, options?: string | WriteFileOptions): void {
  writeAll(path, data, options, "w", false);
}
export function writeFileSync__encoding(path: string, data: string | Uint8Array, options?: string | WriteFileOptions): void {
  writeAll(path, data, options, "w", true);
}

/** Adds data at the end of a file, creating it when missing. */
export function appendFileSync(path: string, data: string | Uint8Array, options?: string | WriteFileOptions): void {
  writeAll(path, data, options, "a", false);
}
export function appendFileSync__utf8(path: string, data: string | Uint8Array, options?: string | WriteFileOptions): void {
  writeAll(path, data, options, "a", false);
}
export function appendFileSync__encoding(path: string, data: string | Uint8Array, options?: string | WriteFileOptions): void {
  writeAll(path, data, options, "a", true);
}

/** Removes a file (or a symbolic link). */
export function unlinkSync(path: string): void {
  __fsCheck(fs_unlink_rc(path), "unlink", path);
}

export function renameSync(oldPath: string, newPath: string): void {
  __fsCheck(fs_rename(oldPath, newPath), "rename", oldPath, newPath);
}

/** Copies a file; mode constants.COPYFILE_EXCL fails when dest exists. */
export function copyFileSync(src: string, dest: string, mode: int = 0): void {
  __fsCheck(fs_copy_file(src, dest, (mode & 1) !== 0), "copyfile", src, dest);
}

export function truncateSync(path: string, len: int = 0): void {
  __fsCheck(fs_truncate(path, len), "open", path);
}

function octal(mode: int | string): int {
  return typeof mode === "string" ? Math.trunc(parseInt(mode, 8)) : mode;
}

export function chmodSync(path: string, mode: int | string): void {
  __fsCheck(fs_chmod(path, octal(mode)), "chmod", path);
}

/** Checks that the file is there (mode F_OK) or can be read, written or run (R_OK, W_OK, X_OK); throws if not. */
export function accessSync(path: string, mode: int = 0): void {
  __fsCheck(fs_access(path, mode), "access", path);
}

function seconds(t: number | Date): number {
  return typeof t === "number" ? t : fs_date_ms(t) / 1000;
}

/** Sets the access and modification times (seconds since the epoch, or Dates). */
export function utimesSync(path: string, atime: number | Date, mtime: number | Date): void {
  __fsCheck(fs_utimes(path, seconds(atime), seconds(mtime), true), "utime", path);
}

/** utimesSync of a symbolic link itself. */
export function lutimesSync(path: string, atime: number | Date, mtime: number | Date): void {
  __fsCheck(fs_utimes(path, seconds(atime), seconds(mtime), false), "lutime", path);
}

export function symlinkSync(target: string, path: string, type: string | null = null): void {
  __fsCheck(fs_symlink(target, path), "symlink", target, path);
}

export function linkSync(existingPath: string, newPath: string): void {
  __fsCheck(fs_link(existingPath, newPath), "link", existingPath, newPath);
}

export function readlinkSync(path: string, options: string | null = null): string {
  const s = fs_readlink(path);
  __fsCheck(fs_last_err(), "readlink", path);
  return s;
}

function parentOf(p: string): string {
  const i = p.lastIndexOf("/");
  return i < 0 ? "." : i === 0 ? "/" : p.slice(0, i);
}

/** The absolute path with the symbolic links resolved. */
export function realpathSync(path: string, options: string | null = null): string {
  const s = fs_realpath(path);
  const r = fs_last_err();
  if (r < 0) {
    // (Node resolves part by part: the error names the path with its existing part resolved)
    let shown = path;
    const i = path.lastIndexOf("/");
    if (__fsCode(r) === "ENOENT" && i >= 0) {
      const dir = fs_realpath(i === 0 ? "/" : path.slice(0, i));
      if (fs_last_err() === 0) shown = (dir === "/" ? "" : dir) + path.slice(i);
    }
    fs_throw(r, "lstat", shown, "");
  }
  return s;
}

/** Makes a directory named prefix followed by six random characters; gives its path. */
export function mkdtempSync(prefix: string, options: string | null = null): string {
  const s = fs_mkdtemp(prefix);
  __fsCheck(fs_last_err(), "mkdtemp", prefix + "XXXXXX");
  return s;
}

// ---- directories ----

// mkdir -p: the first directory made (null, undefined in Node, when all were there); errors name `asked`, the path asked for
function mkdirp(path: string, mode: int, asked: string): string | null {
  const r = fs_mkdir(path, mode);
  if (r === 0) return path;
  const code = __fsCode(r);
  if (code === "EEXIST") {
    const st = fs_stat(path, true, -1);
    if (st.length > 0 && (Math.trunc(st[1]) & 0o170000) === 0o040000) return null;
    fs_throw(r, "mkdir", asked, "");
  }
  if (code !== "ENOENT") fs_throw(r, "mkdir", asked, "");
  const parent = parentOf(path);
  if (parent === path) fs_throw(r, "mkdir", asked, "");
  const first = mkdirp(parent, mode, asked);
  const r2 = fs_mkdir(path, mode);
  if (r2 < 0 && __fsCode(r2) !== "EEXIST") fs_throw(r2, "mkdir", asked, "");
  return first !== null ? first : r2 === 0 ? path : null;
}

/** Makes a directory; with { recursive: true } its missing parents too, and gives the first one it made. */
export function mkdirSync(path: string, options?: int | MkdirOptions): string | null {
  let mode: int = 0o777;
  let recursive = false;
  if (typeof options === "number") {
    mode = options;
  } else if (options !== null) {
    mode = options.mode ?? 0o777;
    recursive = options.recursive === true;
  }
  if (!recursive) {
    __fsCheck(fs_mkdir(path, mode), "mkdir", path);
    return null;
  }
  return mkdirp(path, mode, path);
}

export function rmdirSync(path: string, options?: RmdirOptions): void {
  if (options !== null && options.recursive === true) {
    rmSync(path, { recursive: true, force: false });
    return;
  }
  __fsCheck(fs_rmdir(path), "rmdir", path);
}

function rmTree(path: string): void {
  const entries = fs_readdir(path);
  __fsCheck(fs_last_err(), "rm", path);
  for (const e of entries) {
    const child = path + "/" + e.slice(1);
    if (e.startsWith("d")) {
      rmTree(child);
    } else {
      __fsCheck(fs_unlink_rc(child), "rm", child);
    }
  }
  __fsCheck(fs_rmdir(path), "rm", path);
}

/** Removes a file or (with { recursive: true }) a directory and what it holds; { force: true }: no error when missing. */
export function rmSync(path: string, options?: RmOptions): void {
  const recursive = options !== null && options.recursive === true;
  const force = options !== null && options.force === true;
  const st = fs_stat(path, false, -1);
  if (st.length === 0) {
    const r = fs_last_err();
    if (force && __fsCode(r) === "ENOENT") return;
    fs_throw(r, "lstat", path, "");
  }
  if ((Math.trunc(st[1]) & 0o170000) === 0o040000) {
    if (!recursive) {
      throw new FsCodeError("SystemError", "ERR_FS_EISDIR", `Path is a directory: rm returned EISDIR (is a directory) ${path}`, 21, "rm", path);
    }
    rmTree(path);
    return;
  }
  __fsCheck(fs_unlink_rc(path), "rm", path);
}

function isDot(name: string): boolean {
  return name === "." || name === "..";
}

// the entries of path (letter + name), recursive: breadth first, names relative to path, as Node lists them
function listDir(path: string, recursive: boolean): string[] {
  const top = fs_readdir(path);
  __fsCheck(fs_last_err(), "scandir", path);
  if (!recursive) return top;
  const out: string[] = [];
  for (const e of top) out.push(e);
  let i = 0;
  while (i < out.length) {
    const e = out[i];
    i++;
    if (!e.startsWith("d")) continue;
    const rel = e.slice(1);
    const sub = fs_readdir(path + "/" + rel);
    if (fs_last_err() < 0) continue;
    for (const s of sub) out.push(s.slice(0, 1) + rel + "/" + s.slice(1));
  }
  return out;
}

/** The names in a directory (sorted as the system lists them); { recursive: true }: those of its subdirectories too. */
export function readdirSync(path: string, options?: string | ReaddirOptions): string[] {
  let recursive = false;
  if (typeof options === "string") {
    recursive = false;
  } else if (options !== null) {
    recursive = options.recursive === true;
  }
  const out: string[] = [];
  for (const e of listDir(path, recursive)) {
    if (!isDot(e.slice(1))) out.push(e.slice(1));
  }
  return out;
}

/** readdirSync(path, { withFileTypes: true }): Dirents (name, parentPath, isFile(), isDirectory()...). */
export function readdirSync__withFileTypes(path: string, options: ReaddirOptions): Dirent[] {
  const out: Dirent[] = [];
  for (const e of listDir(path, options.recursive === true)) {
    const rel = e.slice(1);
    const cut = rel.lastIndexOf("/");
    const parent = cut < 0 ? path : path + "/" + rel.slice(0, cut);
    const name = cut < 0 ? rel : rel.slice(cut + 1);
    let kind = e.slice(0, 1);
    if (kind === "u") {
      const st = fs_stat(parent + "/" + name, false, -1);
      if (st.length > 0) kind = new Stats(st).isDirectory() ? "d" : new Stats(st).isSymbolicLink() ? "l" : "f";
    }
    out.push(new Dirent(name, parent, kind));
  }
  return out;
}

// ---- stat ----

/** The file's Stats (size, mode, times, isFile(), isDirectory()...); symbolic links followed. */
export function statSync(path: string, options?: StatOptions): Stats {
  const v = fs_stat(path, true, -1);
  __fsCheck(fs_last_err(), "stat", path);
  return new Stats(v);
}

/** statSync(path, { throwIfNoEntry: false }): undefined when the file is not there. */
export function statSync__throwIfNoEntry_false(path: string, options: StatOptions): Stats | null {
  const v = fs_stat(path, true, -1);
  const r = fs_last_err();
  if (r < 0 && (__fsCode(r) === "ENOENT" || __fsCode(r) === "ENOTDIR")) return null;
  __fsCheck(r, "stat", path);
  return new Stats(v);
}

/** statSync of a symbolic link itself. */
export function lstatSync(path: string, options?: StatOptions): Stats {
  const v = fs_stat(path, false, -1);
  __fsCheck(fs_last_err(), "lstat", path);
  return new Stats(v);
}

export function lstatSync__throwIfNoEntry_false(path: string, options: StatOptions): Stats | null {
  const v = fs_stat(path, false, -1);
  const r = fs_last_err();
  if (r < 0 && (__fsCode(r) === "ENOENT" || __fsCode(r) === "ENOTDIR")) return null;
  __fsCheck(r, "lstat", path);
  return new Stats(v);
}

/** The Stats of what a file descriptor is open on. */
export function fstatSync(fd: int): Stats {
  const v = fs_stat("", true, fd);
  __fsCheck(fs_last_err(), "fstat", "");
  return new Stats(v);
}

// ---- cp ----

function cpTree(src: string, dest: string, o: CopyOptions): void {
  const filter = o.filter;
  if (filter !== null && !filter(src, dest)) return;
  const st = fs_stat(src, o.dereference === true, -1);
  __fsCheck(fs_last_err(), "lstat", src);
  const mode = Math.trunc(st[1]) & 0o170000;
  if (mode === 0o040000) {
    if (o.recursive !== true) {
      throw new FsCodeError("Error", "ERR_FS_EISDIR", `Recursive option not enabled, cannot copy a directory: ${src}/`, 0, "", "");
    }
    const d = fs_stat(dest, true, -1);
    if (d.length === 0) __fsCheck(fs_mkdir(dest, Math.trunc(st[1]) & 0o7777), "mkdir", dest);
    const entries = fs_readdir(src);
    __fsCheck(fs_last_err(), "scandir", src);
    for (const e of entries) cpTree(src + "/" + e.slice(1), dest + "/" + e.slice(1), o);
    return;
  }
  const exists = fs_stat(dest, false, -1).length > 0;
  if (exists && o.force === false) {
    if (o.errorOnExist === true) {
      throw new FsCodeError("SystemError", "ERR_FS_CP_EEXIST", `Target already exists: cp returned EEXIST (${dest} already exists) ${dest}`, 17, "cp", dest);
    }
    return;
  }
  if (mode === 0o120000) {
    const target = fs_readlink(src);
    __fsCheck(fs_last_err(), "readlink", src);
    if (exists) fs_unlink_rc(dest);
    __fsCheck(fs_symlink(target, dest), "symlink", target, dest);
    return;
  }
  __fsCheck(fs_copy_file(src, dest, false), "copyfile", src, dest);
  if (o.preserveTimestamps === true) fs_utimes(dest, st[10] / 1000, st[11] / 1000, true);
}

/** Copies a file, or (with { recursive: true }) a directory and what it holds. */
export function cpSync(src: string, dest: string, options?: CopyOptions): void {
  cpTree(src, dest, options !== null ? options : {});
}

// ---- file descriptors: writeSync, and the callback calls open, close, read, write (and exists) ----
// (here rather than in xisto:fs_callbacks: their names are too common to find in a program's text, and code that is
// not reached is not compiled, so a program that does not call them does not get the event loop for them)

// the bytes of text in an encoding (UTF-8 by default)
function textBytes(text: string, enc: string | null): Uint8Array {
  if (enc === null || enc === "utf8" || enc === "utf-8") return new TextEncoder().encode(text);
  return Buffer.alloc(Buffer.byteLength(text, enc), text, enc);
}
// Node's checks of offset and length against the buffer (ERR_OUT_OF_RANGE), in Node's order and with its texts
function checkSpan(b: Uint8Array, off: int, len: int, write: boolean): void {
  const range = (what: string, must: string, got: int): Error =>
    new FsCodeError("RangeError", "ERR_OUT_OF_RANGE", `The value of "${what}" is out of range. It must be ${must}. Received ${got}`, 0, "", "");
  if (off < 0) throw range("offset", ">= 0 && <= 9007199254740991", off);
  if (write) {
    if (off > b.length) throw range("offset", `<= ${b.length}`, off);
    if (len > b.length - off) throw range("length", `<= ${b.length - off}`, len);
    if (len < 0) throw range("length", ">= 0", len);
    return;
  }
  if (len < 0) throw range("length", ">= 0", len);
  if (len > b.length - off) throw range("length", `<= ${b.length - off}`, len);
}
function writeFd(fd: int, data: string | Uint8Array, a: int | null, b: int | string | null, c: int | null): int {
  let r: int = 0;
  if (typeof data === "string") {
    const bytes = textBytes(data as string, typeof b === "string" ? (b as string) : null);
    r = fs_write_fd(fd, bytes, 0, bytes.length, a ?? -1);
  } else {
    const buf = data as Uint8Array;
    const off: int = a ?? 0;
    const len: int = typeof b === "number" ? (b as int) : buf.length - off;
    checkSpan(buf, off, len, true);
    r = fs_write_fd(fd, buf, off, len, c ?? -1);
  }
  __fsCheck(r, "write", "");
  return r;
}

/** Writes to a file descriptor and gives the number of bytes written: a buffer (from offset, length bytes, at
 * position: null for where the descriptor is) or text (at position, in the encoding: UTF-8 by default). */
export function writeSync(fd: int, data: string | Uint8Array, offsetOrPosition: int | null = null, lengthOrEncoding: int | string | null = null,
  position: int | null = null): int {
  return writeFd(fd, data, offsetOrPosition, lengthOrEncoding, position);
}

const OPEN_FLAGS: string = " r rs sr r+ rs+ sr+ w wx xw w+ wx+ xw+ a ax xa as sa a+ ax+ xa+ as+ sa+ ";

// the error a failed call gives its callback (Node's, as the synchronous call throws it)
function fdError(r: int, syscall: string, path: string): Error | null {
  if (r >= 0) return null;
  try {
    __fsCheck(r, syscall, path);
  } catch (e) {
    return e as Error;
  }
  return null;
}

/** What open() gives its callback: an error, or the file descriptor. */
export type OpenCallback = (err: Error | null, fd: int) => void;
/** What read() gives its callback: the number of bytes read and the buffer. */
export type ReadCallback = (err: Error | null, bytesRead: int, buffer: Uint8Array) => void;
/** What write() gives its callback: the number of bytes written and what was written. */
export type WriteCallback = (err: Error | null, written: int, data: string | Uint8Array) => void;

/** Opens a file (flags "r" by default, mode 0o666) and gives its descriptor to the callback. */
export function open(path: string, flags: string | OpenCallback, mode: int | OpenCallback | null = null, callback: OpenCallback | null = null): void {
  let fl = "r";
  let md: int = 0o666;
  let cb: OpenCallback | null = callback;
  if (typeof flags === "string") fl = flags as string;
  else cb = flags as OpenCallback;
  if (typeof mode === "number") md = mode as int;
  else if (mode !== null) cb = mode as OpenCallback;
  if (OPEN_FLAGS.indexOf(" " + fl + " ") < 0) {
    throw new FsCodeError("TypeError", "ERR_INVALID_ARG_VALUE", `The argument 'flags' is invalid. Received '${fl}'`, 0, "", "");
  }
  const done: OpenCallback = cb as OpenCallback;
  setImmediate((): void => {
    const fd = fs_open_fd(path, fl, md);
    done(fdError(fd, "open", path), fd < 0 ? -1 : fd);
  });
}

/** Closes a file descriptor; the callback gets the error, if any. */
export function close(fd: int, callback: ((err: Error | null) => void) | null = null): void {
  setImmediate((): void => {
    const r = fs_close_fd(fd);
    if (callback !== null) callback(fdError(r, "close", ""));
  });
}

/** Reads from a file descriptor into buffer (from offset, length bytes, at position: null for where the descriptor
 * is); the callback gets the number of bytes read (0 at the end) and the buffer. Also read(fd, buffer, callback). */
export function read(fd: int, buffer: Uint8Array, offset: int | ReadCallback, length: int | null = null, position: int | null = null,
  callback: ReadCallback | null = null): void {
  let off: int = 0;
  let cb: ReadCallback | null = callback;
  if (typeof offset === "number") off = offset as int;
  else cb = offset as ReadCallback;
  const len: int = length ?? buffer.length - off;
  checkSpan(buffer, off, len, false);
  const done: ReadCallback = cb as ReadCallback;
  setImmediate((): void => {
    const r = fs_read_fd(fd, buffer, off, len, position ?? -1);
    done(fdError(r, "read", ""), r < 0 ? 0 : r, buffer);
  });
}

/** Writes to a file descriptor: write(fd, buffer, offset?, length?, position?, callback) or
 * write(fd, text, position?, encoding?, callback); the callback gets the number of bytes written and the data. */
export function write(fd: int, data: string | Uint8Array, a: int | string | WriteCallback | null = null, b: int | string | WriteCallback | null = null,
  c: int | WriteCallback | null = null, callback: WriteCallback | null = null): void {
  let cb: WriteCallback | null = callback;
  let x: int | null = null;
  let y: int | string | null = null;
  let z: int | null = null;
  if (typeof a === "function") cb = a as WriteCallback;
  else if (typeof a === "number") x = a as int;
  if (typeof b === "function") cb = b as WriteCallback;
  else if (typeof b === "number") y = b as int;
  else if (typeof b === "string") y = b as string;
  if (typeof c === "function") cb = c as WriteCallback;
  else if (typeof c === "number") z = c as int;
  const done: WriteCallback = cb as WriteCallback;
  setImmediate((): void => {
    let err: Error | null = null;
    let n: int = 0;
    try {
      n = writeFd(fd, data, x, y, z);
    } catch (e) {
      err = e as Error;
    }
    done(err, n, data);
  });
}

/** Whether the path exists, to the callback (deprecated in Node: use stat or access). */
export function exists(path: string, callback: (exists: boolean) => void): void {
  setImmediate((): void => callback(__existsSync(path)));
}

// ---- glob: Node's lib/internal/fs/glob.js (the walk, so the results come in Node's order) over minimatch's parsing
// of the patterns (braces expanded, ., .. and ** runs simplified; *, ? and [...] matched as its regular expressions
// would, not matching a leading dot; on macOS those match without case, as Node's). Not here: extglobs (+(a|b)...), followSymlinks,
// maxDepth, an exclude function given Dirents (withFileTypes: it gets the path).

/** globSync()'s options: cwd (where relative patterns start), exclude (patterns, or a function of the path), and
 * withFileTypes (Dirents instead of paths). */
export interface GlobOptions {
  cwd?: string | null;
  exclude?: ((path: string) => boolean) | string[] | null;
  withFileTypes?: boolean | null;
}

// one part of a pattern: kind 0 a name, 1 **, 2 a part with *, ? or [...] (nocase: matched without case)
class GlobPart {
  kind: int;
  s: string;
  nocase: boolean;
  constructor(kind: int, s: string, nocase: boolean) {
    this.kind = kind;
    this.s = s;
    this.nocase = nocase;
  }
}

// {a,b} and {1..3} expanded, the leftmost first (brace-expansion's order)
function braceExpand(p: string): string[] {
  let depth = 0;
  let open = -1;
  for (let i = 0; i < p.length; i++) {
    const c = p.charAt(i);
    if (c === "\\") {
      i++;
      continue;
    }
    if (c === "{") {
      if (depth === 0) open = i;
      depth++;
    } else if (c === "}" && depth > 0) {
      depth--;
      if (depth === 0) {
        const body = p.slice(open + 1, i);
        const pre = p.slice(0, open);
        const post = p.slice(i + 1);
        const alts: string[] = [];
        let d = 0;
        let from = 0;
        for (let k = 0; k < body.length; k++) {
          const b = body.charAt(k);
          if (b === "{") d++;
          else if (b === "}") d--;
          else if (b === "," && d === 0) {
            alts.push(body.slice(from, k));
            from = k + 1;
          }
        }
        alts.push(body.slice(from));
        if (alts.length === 1) {
          const dots = body.indexOf("..");
          const a = dots > 0 ? Number(body.slice(0, dots)) : NaN;
          const b = dots > 0 ? Number(body.slice(dots + 2)) : NaN;
          if (Number.isInteger(a) && Number.isInteger(b)) {
            alts.length = 0;
            const step = a <= b ? 1 : -1;
            for (let n = a; step > 0 ? n <= b : n >= b; n += step) alts.push(String(n));
          } else {
            const rest = braceExpand(post);
            const out: string[] = [];
            for (const r of rest) out.push(pre + "{" + body + "}" + r);
            return out;
          }
        }
        const out: string[] = [];
        for (const a of alts) for (const x of braceExpand(pre + a + post)) out.push(x);
        return out;
      }
    }
  }
  return [p];
}

// minimatch's simplification of a pattern's parts (optimizationLevel 2, as far as it changes what is walked)
function globSegments(pattern: string): string[] {
  const raw = pattern.replaceAll("\\", "/").split("/");
  let parts: string[] = [];
  for (let i = 0; i < raw.length; i++) {
    const s = raw[i];
    if (s === "" && i > 0 && i < raw.length - 1) continue;
    if (s === "." && raw.length > 1 && !(i === raw.length - 1 && parts.length === 0)) continue;
    if (s === "**" && parts.length > 0 && parts[parts.length - 1] === "**") continue;
    parts.push(s);
  }
  for (let changed = true; changed;) {
    changed = false;
    for (let i = 1; i < parts.length; i++) {
      const prev = parts[i - 1];
      if (parts[i] === ".." && prev !== ".." && prev !== "**" && prev !== "" && !globMagic(prev)) {
        const next: string[] = [];
        for (let k = 0; k < parts.length; k++) if (k !== i && k !== i - 1) next.push(parts[k]);
        parts = next;
        changed = true;
        break;
      }
    }
  }
  if (parts.length === 0) parts.push(".");
  return parts;
}

function globMagic(s: string): boolean {
  for (let i = 0; i < s.length; i++) {
    const c = s.charAt(i);
    if (c === "*" || c === "?" || c === "[") return true;
  }
  return false;
}

// a name against a part with *, ? or [...] (minimatch's regular expression, matched by hand: a RegExp here would load
// xisto:errors into every program that uses node:fs): the whole name, no leading dot unless the part starts with one
function globSegMatch(pat: string, name: string, nocase: boolean): boolean {
  if (name.startsWith(".") && !pat.startsWith(".")) return false;
  const p = nocase ? pat.toLowerCase() : pat;
  const n = nocase ? name.toLowerCase() : name;
  return globMatchAt(p, 0, n, 0);
}
function globMatchAt(p: string, pi: int, n: string, ni: int): boolean {
  while (pi < p.length) {
    const c = p.charAt(pi);
    if (c === "*") {
      while (pi < p.length && p.charAt(pi) === "*") pi++;
      if (pi === p.length) return true;
      for (let k = ni; k <= n.length; k++) if (globMatchAt(p, pi, n, k)) return true;
      return false;
    }
    if (ni >= n.length) return false;
    if (c === "?") {
      pi++;
      ni++;
      continue;
    }
    if (c === "[") {
      const close = p.indexOf("]", pi + 2);
      if (close >= 0) {
        let body = p.slice(pi + 1, close);
        let neg = false;
        if (body.startsWith("!") || body.startsWith("^")) {
          neg = true;
          body = body.slice(1);
        }
        const ch = n.charAt(ni);
        let hit = false;
        for (let j = 0; j < body.length; j++) {
          if (j + 2 < body.length && body.charAt(j + 1) === "-") {
            if (ch >= body.charAt(j) && ch <= body.charAt(j + 2)) hit = true;
            j += 2;
          } else if (body.charAt(j) === ch) {
            hit = true;
          }
        }
        if (hit === neg) return false;
        pi = close + 1;
        ni++;
        continue;
      }
    }
    if (c !== n.charAt(ni)) return false;
    pi++;
    ni++;
  }
  return ni === n.length;
}

function globParts(segs: string[], nocase: boolean): GlobPart[] {
  const out: GlobPart[] = [];
  for (const s of segs) {
    if (s === "**") out.push(new GlobPart(1, s, false));
    else if (globMagic(s)) out.push(new GlobPart(2, s, nocase));
    else out.push(new GlobPart(0, s, false));
  }
  return out;
}

// a whole path against a pattern's parts (exclude patterns): ** any number of names, none starting with a dot
function globMatchFrom(parts: GlobPart[], pi: int, segs: string[], si: int): boolean {
  if (pi === parts.length) return si === segs.length;
  const p = parts[pi];
  if (p.kind === 1) {
    if (globMatchFrom(parts, pi + 1, segs, si)) return true;
    if (si < segs.length && !segs[si].startsWith(".") && segs[si] !== "") return globMatchFrom(parts, pi, segs, si + 1);
    return false;
  }
  if (si >= segs.length) return false;
  if (p.kind === 0) {
    if (p.s.toLowerCase() !== segs[si].toLowerCase()) return false;
    if (!globNocase() && p.s !== segs[si]) return false;
  } else if (!globSegMatch(p.s, segs[si], p.nocase)) {
    return false;
  }
  return globMatchFrom(parts, pi + 1, segs, si + 1);
}

declare function url_platform(): string;
// (the runtime's platform name: naming process here would load xisto:process into every node:fs program)
function globNocase(): boolean {
  const p = url_platform();
  return p === "darwin" || p === "win32";
}

class GlobPattern {
  parts: GlobPart[];
  strings: string[];
  indexes: Set<int>;
  symlinks: Set<int>;
  isAbsolute: boolean;
  last: int;
  constructor(parts: GlobPart[], strings: string[], indexes: Set<int>, symlinks: Set<int>, isAbsolute: boolean) {
    this.parts = parts;
    this.strings = strings;
    this.indexes = indexes;
    this.symlinks = symlinks;
    this.isAbsolute = isAbsolute;
    this.last = parts.length - 1;
  }
  part(i: int): GlobPart | null {
    const k = i < 0 ? this.parts.length + i : i;
    return k >= 0 && k < this.parts.length ? this.parts[k] : null;
  }
  isLit(i: int, s: string): boolean {
    const p = this.part(i);
    return p !== null && p.kind === 0 && p.s === s;
  }
  isStar(i: int): boolean {
    const p = this.part(i);
    return p !== null && p.kind === 1;
  }
  isLast(isDir: boolean): boolean {
    return this.indexes.has(this.last) || (this.isLit(-1, "") && isDir && this.indexes.has(this.last - 1) && this.isStar(-2));
  }
  hasSeenSymlinks(): boolean {
    for (const i of this.indexes) if (!this.symlinks.has(i)) return true;
    return false;
  }
  childWith(indexes: Set<int>, symlinks: Set<int> = new Set<int>()): GlobPattern {
    return new GlobPattern(this.parts, this.strings, indexes, symlinks, this.isAbsolute);
  }
  testAt(i: int, name: string): boolean {
    const p = this.part(i);
    if (p === null || i < 0) return false;
    if (p.kind === 1) return true;
    if (p.kind === 0) return p.s === name;
    return globSegMatch(p.s, name, p.nocase);
  }
  cacheKey(i: int): string {
    let key = "";
    for (let k = i; k < this.strings.length; k++) {
      key += this.strings[k];
      if (k !== this.strings.length - 1) key += "/";
    }
    return key;
  }
}

// an exclude pattern's parts (a class, not a GlobPart[][]: an array of arrays is made where it is typed, so its
// element class would be compiled into every node:fs program)
class GlobExclude {
  parts: GlobPart[];
  constructor(parts: GlobPart[]) {
    this.parts = parts;
  }
}

class GlobEntry {
  name: string;
  kind: string; // the runtime's letter: f d l ... ("" when the path does not exist)
  constructor(name: string, kind: string) {
    this.name = name;
    this.kind = kind;
  }
  isDir(): boolean {
    return this.kind === "d";
  }
  isLink(): boolean {
    return this.kind === "l";
  }
}

class GlobQueueItem {
  path: string;
  patterns: GlobPattern[];
  constructor(path: string, patterns: GlobPattern[]) {
    this.path = path;
    this.patterns = patterns;
  }
}

function globKind(path: string): string {
  const v = fs_stat(path, false, -1);
  if (v.length === 0) return "";
  const mode = Math.trunc(v[1]) & 0o170000; // (the type bits only: a Stats of / can hold numbers past an int)
  return mode === 0o040000 ? "d" : mode === 0o120000 ? "l" : "f";
}

class GlobWalk {
  root: string;
  excludeFn: ((path: string) => boolean) | null = null;
  excludeParts: GlobExclude[] = [];
  results: string[] = [];
  private resultSet: Set<string> = new Set<string>();
  private queue: GlobQueueItem[] = [];
  private sub: Map<string, GlobPattern[]> = new Map<string, GlobPattern[]>();
  private seenKeys: Map<string, Set<string>> = new Map<string, Set<string>>();
  private stats: Map<string, string> = new Map<string, string>();
  private dirs: Map<string, GlobEntry[]> = new Map<string, GlobEntry[]>();
  patterns: GlobPattern[] = [];

  constructor(root: string) {
    this.root = root;
  }
  kind(path: string): string {
    const c = this.stats.get(path);
    if (c !== undefined && c !== null) return c as string;
    const k = globKind(path);
    this.stats.set(path, k);
    return k;
  }
  readdir(path: string): GlobEntry[] {
    const c = this.dirs.get(path);
    if (c !== undefined && c !== null) return c as GlobEntry[];
    const out: GlobEntry[] = [];
    const raw = fs_readdir(path);
    if (fs_last_err() >= 0) {
      for (const e of raw) {
        let k = e.slice(0, 1);
        if (k === "u") k = globKind(path + "/" + e.slice(1));
        out.push(new GlobEntry(e.slice(1), k));
      }
    }
    this.dirs.set(path, out);
    return out;
  }
  // true when the path was walked with these pattern indexes already
  private add(path: string, pattern: GlobPattern): boolean {
    let found = this.seenKeys.get(path);
    if (found === undefined || found === null) {
      found = new Set<string>();
      this.seenKeys.set(path, found as Set<string>);
    }
    const set = found as Set<string>;
    const before = set.size;
    for (const i of pattern.indexes) set.add(pattern.cacheKey(i));
    return set.size !== before + pattern.indexes.size;
  }
  private seen(path: string, pattern: GlobPattern, i: int): boolean {
    const set = this.seenKeys.get(path);
    return set !== undefined && set !== null && (set as Set<string>).has(pattern.cacheKey(i));
  }
  isExcluded(full: string): boolean {
    if (this.excludeParts.length === 0) return false;
    const segs = full.split("/");
    for (const x of this.excludeParts) if (globMatchFrom(x.parts, 0, segs, 0)) return true;
    return false;
  }
  private addResult(path: string): void {
    if (this.isExcluded(__presolve(this.root, path))) return;
    if (this.resultSet.has(path)) return;
    this.resultSet.add(path);
    this.results.push(path);
  }
  private addSubpattern(path: string, pattern: GlobPattern): void {
    if (this.isExcluded(path)) return;
    const full = __presolve(this.root, path);
    if (this.isExcluded(full + "/") && this.kind(full) === "d") return;
    const fn = this.excludeFn;
    if (fn !== null && fn(path)) return;
    const list = this.sub.get(path);
    if (list === undefined || list === null) this.sub.set(path, [pattern]);
    else (list as GlobPattern[]).push(pattern);
  }
  run(): void {
    this.queue.push(new GlobQueueItem(".", this.patterns));
    while (this.queue.length > 0) {
      const item = this.queue.pop() as GlobQueueItem;
      for (const p of item.patterns) this.addSubpatterns(item.path, p);
      // (keys, then get: a [path, patterns] pair would be a tuple class made where it is typed, and with it these
      // classes in every program that loads node:fs)
      for (const path of this.sub.keys()) this.queue.push(new GlobQueueItem(path, this.sub.get(path) as GlobPattern[]));
      this.sub.clear();
    }
  }
  one0(): Set<int> {
    return this.one(0);
  }
  one(i: int): Set<int> {
    const s = new Set<int>();
    s.add(i);
    return s;
  }
  private addSubpatterns(path: string, pattern: GlobPattern): void {
    if (this.add(path, pattern)) return;
    const full = __presolve(this.root, path);
    const kind = this.kind(full);
    const last = pattern.last;
    const isDir = kind === "d" || (kind === "l" && pattern.hasSeenSymlinks());
    const isLast = pattern.isLast(isDir);
    const isFirst = pattern.indexes.has(0);
    if (this.isExcluded(full)) return;
    if (isFirst && pattern.isLit(0, "")) {
      this.addSubpattern("/", pattern.childWith(this.one(1)));
      return;
    }
    if (isFirst && pattern.isLit(0, "..")) {
      this.addSubpattern("../", pattern.childWith(this.one(1)));
      return;
    }
    if (isFirst && pattern.isLit(0, ".")) {
      this.addSubpattern(".", pattern.childWith(this.one(1)));
      return;
    }
    const lastPart = pattern.part(-1) as GlobPart;
    if (isLast && lastPart.kind === 0) {
      const p = lastPart.s;
      const k = this.kind(__pjoin(full, p));
      if (k !== "" && (p.length > 0 || isDir)) this.addResult(__pjoin(path, p));
      if (pattern.indexes.size === 1 && pattern.indexes.has(last)) return;
    } else if (isLast && lastPart.kind === 1 && (path !== "." || pattern.isLit(0, ".") || (last === 0 && kind !== ""))) {
      this.addResult(path);
    }
    if (!isDir) return;
    let children: GlobEntry[] = [];
    let firstLit = "";
    let single = false;
    if (pattern.indexes.size === 1) {
      for (const i of pattern.indexes) {
        const p0 = pattern.part(i);
        if (p0 !== null && p0.kind === 0) {
          firstLit = p0.s;
          single = true;
        }
      }
    }
    if (single) {
      const k = this.kind(__pjoin(full, firstLit));
      if (k === "") return;
      children = [new GlobEntry(firstLit, k)];
    } else {
      children = this.readdir(full);
    }
    for (const entry of children) {
      const entryPath = __pjoin(path, entry.name);
      const entryFull = __pjoin(full, entry.name);
      this.stats.set(entryFull, entry.kind);
      const entryIsDir = entry.isDir();
      const subs = new Set<int>();
      const nSymlinks = new Set<int>();
      for (const index of pattern.indexes) {
        const current = pattern.part(index);
        const nextIndex = index + 1;
        const fromSymlink = pattern.symlinks.has(index);
        if (current !== null && current.kind === 1) {
          const isDot = entry.name.startsWith(".");
          const nextMatches = pattern.testAt(nextIndex, entry.name);
          let nextNonGlob = nextIndex;
          while (pattern.isStar(nextNonGlob)) nextNonGlob++;
          const matchesDot = isDot && pattern.testAt(nextNonGlob, entry.name);
          const fn = this.excludeFn;
          if ((isDot && !matchesDot) || (fn !== null && fn(entry.name))) continue;
          if (!fromSymlink && entryIsDir) subs.add(index);
          else if (!fromSymlink && index === last) this.addResult(entryPath);
          if (nextMatches && nextIndex === last && !isLast) this.addResult(entryPath);
          else if (nextMatches && entryIsDir) subs.add(index + 2);
          if ((nextMatches || pattern.isLit(0, ".")) && entryIsDir && !fromSymlink) subs.add(nextIndex);
          if (entry.isLink()) nSymlinks.add(index);
          if (pattern.isLit(nextIndex, "..") && entryIsDir) {
            const parent = __pjoin(path, "..");
            if (nextIndex < last) {
              if (!this.sub.has(path) && !this.seen(path, pattern, nextIndex + 1)) this.sub.set(path, [pattern.childWith(this.one(nextIndex + 1))]);
              if (!this.sub.has(parent) && !this.seen(parent, pattern, nextIndex + 1)) this.sub.set(parent, [pattern.childWith(this.one(nextIndex + 1))]);
            } else {
              if (!this.seen(path, pattern, nextIndex)) {
                this.add(path, pattern.childWith(this.one(nextIndex)));
                this.addResult(path);
              }
              if (!this.seen(path, pattern, nextIndex) || !this.seen(parent, pattern, nextIndex)) {
                this.add(parent, pattern.childWith(this.one(nextIndex)));
                this.addResult(parent);
              }
            }
          }
        }
        if (current !== null && current.kind === 0) {
          if (pattern.testAt(index, entry.name) && index !== last) {
            subs.add(nextIndex);
          } else if (current.s === "." && pattern.testAt(nextIndex, entry.name)) {
            if (nextIndex === last) this.addResult(entryPath);
            else subs.add(nextIndex + 1);
          }
        }
        if (current !== null && current.kind === 2 && pattern.testAt(index, entry.name)) {
          if (index === last) this.addResult(entryPath);
          else if (entryIsDir) subs.add(nextIndex);
        }
      }
      if (subs.size > 0) this.addSubpattern(entryPath, pattern.childWith(subs, nSymlinks));
    }
  }
}

function globWalk(pattern: string | string[], options: GlobOptions | null): GlobWalk {
  const cwd = options !== null ? options.cwd ?? "." : ".";
  const w = new GlobWalk(cwd);
  const nocase = globNocase();
  if (options !== null) {
    const ex = options.exclude;
    if (ex !== null && ex !== undefined) {
      if (Array.isArray(ex)) {
        for (const e of ex as string[]) {
          for (const x of braceExpand(__presolve(cwd, e))) w.excludeParts.push(new GlobExclude(globParts(globSegments(x), true)));
        }
      } else {
        w.excludeFn = ex as (path: string) => boolean;
      }
    }
  }
  const list: string[] = typeof pattern === "string" ? [pattern as string] : (pattern as string[]);
  for (const p of list) {
    for (const x of braceExpand(p)) {
      const segs = globSegments(x);
      w.patterns.push(new GlobPattern(globParts(segs, nocase), segs, w.one0(), new Set<int>(), segs.length > 0 && segs[0] === ""));
    }
  }
  w.run();
  return w;
}

/** The paths that match the pattern (or any of the patterns), in Node's order. */
export function globSync(pattern: string | string[], options: GlobOptions | null = null): string[] {
  return globWalk(pattern, options).results;
}

/** globSync with { withFileTypes: true }: a Dirent for each match. */
export function globSync__withFileTypes(pattern: string | string[], options: GlobOptions): Dirent[] {
  const w = globWalk(pattern, options);
  const out: Dirent[] = [];
  for (const r of w.results) {
    const full = r.startsWith("/") ? r : __pjoin(w.root, r);
    out.push(new Dirent(__pbasename(full), __pdirname(full), globKind(full)));
  }
  return out;
}

/** The matches, to the callback (on a later turn of the event loop). */
export function glob(pattern: string | string[], options: GlobOptions | ((err: Error | null, matches: string[]) => void) | null,
  callback: ((err: Error | null, matches: string[]) => void) | null = null): void {
  let opts: GlobOptions | null = null;
  let cb = callback;
  if (typeof options === "function") cb = options as (err: Error | null, matches: string[]) => void;
  else if (options !== null) opts = options as GlobOptions;
  const done = cb as (err: Error | null, matches: string[]) => void;
  setImmediate((): void => {
    let err: Error | null = null;
    let out: string[] = [];
    try {
      out = globSync(pattern, opts);
    } catch (e) {
      err = e as Error;
    }
    done(err, out);
  });
}
