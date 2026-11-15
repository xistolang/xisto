// xisto:fs_callbacks: node:fs's callback API (fs.readFile(path, options?, callback) & co), over the synchronous calls
// of xisto:fs. The call runs on a later turn of the event loop (Node runs it on its thread pool) and the callback gets
// (err, result): err null when it worked. On an error the result is empty ("", an empty Buffer or array, a zero
// Stats) where Node gives undefined. Loaded only by the programs that name one of these (src/parser.c, fs_extras).
import {
  readFileSync, writeFileSync, appendFileSync, mkdirSync, readdirSync, statSync, lstatSync, fstatSync, rmSync, rmdirSync,
  renameSync, accessSync, unlinkSync, copyFileSync, cpSync, realpathSync, mkdtempSync, readlinkSync, symlinkSync, linkSync,
  chmodSync, truncateSync, utimesSync, Stats, Dirent, MkdirOptions, RmOptions, RmdirOptions, ReaddirOptions, StatOptions,
  WriteFileOptions, CopyOptions,
} from "xisto:fs";

export type NoParamCallback = (err: Error | null) => void;
type BytesCallback = (err: Error | null, data: Buffer) => void;
type TextCallback = (err: Error | null, data: string) => void;
type NamesCallback = (err: Error | null, files: string[]) => void;
type DirentsCallback = (err: Error | null, files: Dirent[]) => void;
type StatsCallback = (err: Error | null, stats: Stats) => void;
type PathCallback = (err: Error | null, path: string | undefined) => void;
export interface ReadFileOptions {
  encoding?: string | null;
  flag?: string;
}

function done(cb: NoParamCallback | undefined, op: () => void): void {
  setImmediate((): void => {
    let err: Error | null = null;
    try {
      op();
    } catch (e) {
      err = e as Error;
    }
    if (cb !== undefined) cb(err);
  });
}

/** The file's bytes: readFile(path, callback(err, data)). */
export function readFile(path: string, options: ReadFileOptions | BytesCallback | null | undefined, callback?: BytesCallback): void {
  const cb = typeof options === "function" ? options : callback;
  setImmediate((): void => {
    let data = Buffer.alloc(0);
    let err: Error | null = null;
    try {
      data = readFileSync(path);
    } catch (e) {
      err = e as Error;
    }
    if (cb !== undefined) cb(err, data);
  });
}

/** readFile(path, "utf8" | { encoding }, callback(err, text)): the file as text. */
export function readFile__encoding(path: string, options: string | ReadFileOptions, callback: TextCallback): void {
  let enc = "utf8";
  if (typeof options === "string") {
    enc = options;
  } else {
    enc = options.encoding ?? "utf8";
  }
  setImmediate((): void => {
    let data = "";
    let err: Error | null = null;
    try {
      data = enc === "utf8" || enc === "utf-8" ? readFileSync(path, "utf8") : readFileSync(path).toString(enc);
    } catch (e) {
      err = e as Error;
    }
    callback(err, data);
  });
}

export function writeFile(path: string, data: string | Uint8Array, options: string | WriteFileOptions | NoParamCallback | undefined, callback?: NoParamCallback): void {
  if (typeof options === "function") {
    done(options, (): void => writeFileSync(path, data));
  } else {
    done(callback, (): void => writeFileSync(path, data, options));
  }
}

export function appendFile(path: string, data: string | Uint8Array, options: string | WriteFileOptions | NoParamCallback | undefined, callback?: NoParamCallback): void {
  if (typeof options === "function") {
    done(options, (): void => appendFileSync(path, data));
  } else {
    done(callback, (): void => appendFileSync(path, data, options));
  }
}

/** mkdir(path, options?, callback(err, path)): path is the first directory made, with { recursive: true }. */
export function mkdir(path: string, options: int | MkdirOptions | PathCallback | undefined, callback?: PathCallback): void {
  const cb = typeof options === "function" ? options : callback;
  setImmediate((): void => {
    let made: string | undefined = undefined;
    let err: Error | null = null;
    try {
      if (typeof options === "function") made = mkdirSync(path);
      else made = mkdirSync(path, options);
    } catch (e) {
      err = e as Error;
    }
    if (cb !== undefined) cb(err, made);
  });
}

export function readdir(path: string, options: string | ReaddirOptions | NamesCallback | undefined, callback?: NamesCallback): void {
  const cb = typeof options === "function" ? options : callback;
  setImmediate((): void => {
    let names: string[] = [];
    let err: Error | null = null;
    try {
      if (typeof options === "function") names = readdirSync(path);
      else names = readdirSync(path, options);
    } catch (e) {
      err = e as Error;
    }
    if (cb !== undefined) cb(err, names);
  });
}

/** readdir(path, { withFileTypes: true }, callback(err, dirents)). */
export function readdir__withFileTypes(path: string, options: ReaddirOptions, callback: DirentsCallback): void {
  setImmediate((): void => {
    let ents: Dirent[] = [];
    let err: Error | null = null;
    try {
      ents = readdirSync(path, { withFileTypes: true, recursive: options.recursive === true });
    } catch (e) {
      err = e as Error;
    }
    callback(err, ents);
  });
}

function statWith(path: string, link: boolean, cb: StatsCallback | undefined): void {
  setImmediate((): void => {
    let st = new Stats([]);
    let err: Error | null = null;
    try {
      st = link ? lstatSync(path) : statSync(path);
    } catch (e) {
      err = e as Error;
    }
    if (cb !== undefined) cb(err, st);
  });
}

export function stat(path: string, options: StatOptions | StatsCallback | undefined, callback?: StatsCallback): void {
  statWith(path, false, typeof options === "function" ? options : callback);
}

export function lstat(path: string, options: StatOptions | StatsCallback | undefined, callback?: StatsCallback): void {
  statWith(path, true, typeof options === "function" ? options : callback);
}

export function fstat(fd: int, callback: StatsCallback): void {
  setImmediate((): void => {
    let st = new Stats([]);
    let err: Error | null = null;
    try {
      st = fstatSync(fd);
    } catch (e) {
      err = e as Error;
    }
    callback(err, st);
  });
}

export function rm(path: string, options: RmOptions | NoParamCallback | undefined, callback?: NoParamCallback): void {
  if (typeof options === "function") {
    done(options, (): void => rmSync(path));
  } else {
    done(callback, (): void => rmSync(path, options));
  }
}

export function rmdir(path: string, options: RmdirOptions | NoParamCallback | undefined, callback?: NoParamCallback): void {
  if (typeof options === "function") {
    done(options, (): void => rmdirSync(path));
  } else {
    done(callback, (): void => rmdirSync(path, options));
  }
}

export function rename(oldPath: string, newPath: string, callback: NoParamCallback): void {
  done(callback, (): void => renameSync(oldPath, newPath));
}

export function access(path: string, mode: int | NoParamCallback | undefined, callback?: NoParamCallback): void {
  if (typeof mode === "function") {
    done(mode, (): void => accessSync(path));
  } else {
    const m: int = mode ?? 0;
    done(callback, (): void => accessSync(path, m));
  }
}

export function unlink(path: string, callback: NoParamCallback): void {
  done(callback, (): void => unlinkSync(path));
}

export function copyFile(src: string, dest: string, mode: int | NoParamCallback | undefined, callback?: NoParamCallback): void {
  if (typeof mode === "function") {
    done(mode, (): void => copyFileSync(src, dest));
  } else {
    const m: int = mode ?? 0;
    done(callback, (): void => copyFileSync(src, dest, m));
  }
}

export function cp(src: string, dest: string, options: CopyOptions | NoParamCallback | undefined, callback?: NoParamCallback): void {
  if (typeof options === "function") {
    done(options, (): void => cpSync(src, dest));
  } else {
    done(callback, (): void => cpSync(src, dest, options));
  }
}

function withPath(cb: TextCallback | undefined, op: () => string): void {
  setImmediate((): void => {
    let s = "";
    let err: Error | null = null;
    try {
      s = op();
    } catch (e) {
      err = e as Error;
    }
    if (cb !== undefined) cb(err, s);
  });
}

export function realpath(path: string, options: string | TextCallback | null | undefined, callback?: TextCallback): void {
  withPath(typeof options === "function" ? options : callback, (): string => realpathSync(path));
}

export function mkdtemp(prefix: string, options: string | TextCallback | null | undefined, callback?: TextCallback): void {
  withPath(typeof options === "function" ? options : callback, (): string => mkdtempSync(prefix));
}

export function readlink(path: string, options: string | TextCallback | null | undefined, callback?: TextCallback): void {
  withPath(typeof options === "function" ? options : callback, (): string => readlinkSync(path));
}

export function symlink(target: string, path: string, type: string | NoParamCallback | null | undefined, callback?: NoParamCallback): void {
  done(typeof type === "function" ? type : callback, (): void => symlinkSync(target, path));
}

export function link(existingPath: string, newPath: string, callback: NoParamCallback): void {
  done(callback, (): void => linkSync(existingPath, newPath));
}

export function chmod(path: string, mode: int | string, callback: NoParamCallback): void {
  done(callback, (): void => chmodSync(path, mode));
}

export function truncate(path: string, len: int | NoParamCallback | undefined, callback?: NoParamCallback): void {
  if (typeof len === "function") {
    done(len, (): void => truncateSync(path));
  } else {
    const n: int = len ?? 0;
    done(callback, (): void => truncateSync(path, n));
  }
}

export function utimes(path: string, atime: number | Date, mtime: number | Date, callback: NoParamCallback): void {
  done(callback, (): void => utimesSync(path, atime, mtime));
}
