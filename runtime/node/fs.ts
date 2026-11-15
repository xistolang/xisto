// xisto:fs (node:fs): Node's synchronous file system API, written in xisto over system calls of the runtime
// (runtime/xisto_rt.h, "node:fs"). readFileSync, existsSync and the file descriptor calls are the runtime's own
// (xisto:fs_native: the checker picks the text or the bytes flavour of readFileSync by its arguments). The callback
// API (fs.readFile & co, runtime/node/fs_callbacks.ts), fs.promises (runtime/node/fs_promises.ts) and the streams
// (runtime/node/stream.ts) are loaded only by the programs that name them (src/parser.c, fs_extras).
// Errors are Node's: "ENOENT: no such file or directory, open '/x'", with err.code, errno, syscall and path (dest
// for the calls on two paths). Paths are strings (not Buffers or URLs: fileURLToPath(url) gives one).
export { readFileSync, existsSync, openSync, closeSync, readSync } from "xisto:fs_native";

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
  ino: int = 0;
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
    this.ino = Math.trunc(v[7]);
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

function writeAll(path: string, data: string | Uint8Array, options: string | WriteFileOptions | undefined, flag: string, other: boolean): void {
  let enc = "utf8";
  let mode: int = 0o666;
  let fl = flag;
  if (typeof options === "string") {
    enc = options;
  } else if (options !== undefined) {
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

// mkdir -p: the first directory made (undefined when all were there); errors name `asked`, the path asked for
function mkdirp(path: string, mode: int, asked: string): string | undefined {
  const r = fs_mkdir(path, mode);
  if (r === 0) return path;
  const code = __fsCode(r);
  if (code === "EEXIST") {
    const st = fs_stat(path, true, -1);
    if (st.length > 0 && (Math.trunc(st[1]) & 0o170000) === 0o040000) return undefined;
    fs_throw(r, "mkdir", asked, "");
  }
  if (code !== "ENOENT") fs_throw(r, "mkdir", asked, "");
  const parent = parentOf(path);
  if (parent === path) fs_throw(r, "mkdir", asked, "");
  const first = mkdirp(parent, mode, asked);
  const r2 = fs_mkdir(path, mode);
  if (r2 < 0 && __fsCode(r2) !== "EEXIST") fs_throw(r2, "mkdir", asked, "");
  return first !== undefined ? first : r2 === 0 ? path : undefined;
}

/** Makes a directory; with { recursive: true } its missing parents too, and gives the first one it made. */
export function mkdirSync(path: string, options?: int | MkdirOptions): string | undefined {
  let mode: int = 0o777;
  let recursive = false;
  if (typeof options === "number") {
    mode = options;
  } else if (options !== undefined) {
    mode = options.mode ?? 0o777;
    recursive = options.recursive === true;
  }
  if (!recursive) {
    __fsCheck(fs_mkdir(path, mode), "mkdir", path);
    return undefined;
  }
  return mkdirp(path, mode, path);
}

export function rmdirSync(path: string, options?: RmdirOptions): void {
  if (options !== undefined && options.recursive === true) {
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
  const recursive = options !== undefined && options.recursive === true;
  const force = options !== undefined && options.force === true;
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
  } else if (options !== undefined) {
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
export function statSync__throwIfNoEntry_false(path: string, options: StatOptions): Stats | undefined {
  const v = fs_stat(path, true, -1);
  const r = fs_last_err();
  if (r < 0 && (__fsCode(r) === "ENOENT" || __fsCode(r) === "ENOTDIR")) return undefined;
  __fsCheck(r, "stat", path);
  return new Stats(v);
}

/** statSync of a symbolic link itself. */
export function lstatSync(path: string, options?: StatOptions): Stats {
  const v = fs_stat(path, false, -1);
  __fsCheck(fs_last_err(), "lstat", path);
  return new Stats(v);
}

export function lstatSync__throwIfNoEntry_false(path: string, options: StatOptions): Stats | undefined {
  const v = fs_stat(path, false, -1);
  const r = fs_last_err();
  if (r < 0 && (__fsCode(r) === "ENOENT" || __fsCode(r) === "ENOTDIR")) return undefined;
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
  if (filter !== undefined && !filter(src, dest)) return;
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
  cpTree(src, dest, options !== undefined ? options : {});
}
