// xisto:fs/promises (node:fs/promises): the promise flavour of node:fs, over the synchronous calls of xisto:fs (each
// runs when its promise is awaited; Node runs them on its thread pool). Errors are Node's: "ENOENT: no such file or
// directory, open '<path>'" with err.code, errno, syscall and path. fs.promises is the class `promises` below.
import {
  readFileSync, writeFileSync, appendFileSync, unlinkSync, mkdirSync, readdirSync, statSync, lstatSync, rmSync, rmdirSync,
  renameSync, copyFileSync, cpSync, mkdtempSync, realpathSync, accessSync, chmodSync, readlinkSync, symlinkSync, linkSync,
  utimesSync, lutimesSync, truncateSync, Stats, Dirent, MkdirOptions, RmOptions, RmdirOptions, ReaddirOptions, StatOptions,
  WriteFileOptions, CopyOptions,
} from "xisto:fs";

export interface ReadFileOptions {
  encoding?: string | null;
  flag?: string;
}

/** The whole file as bytes (a Buffer). */
export async function readFile(path: string, options: ReadFileOptions | null = null): Promise<Buffer> {
  return readFileSync(path);
}

/**
 * The whole file as text: readFile(path, "utf8") or readFile(path, { encoding: "utf8" }).
 * @example const pem = await readFile("key.pem", "utf-8");
 */
export async function readFile__encoding(path: string, options: string | ReadFileOptions): Promise<string> {
  let enc = "utf8";
  if (typeof options === "string") {
    enc = options;
  } else {
    enc = options.encoding ?? "utf8";
  }
  if (enc === "utf8" || enc === "utf-8") {
    return readFileSync(path, "utf8");
  }
  return readFileSync(path).toString(enc);
}

/** Writes data to a file, replacing it (text as UTF-8 unless an encoding says otherwise). */
export async function writeFile(path: string, data: string | Uint8Array, options?: string | WriteFileOptions): Promise<void> {
  writeFileSync(path, data, options);
}

/** Adds data at the end of a file, creating it when missing. */
export async function appendFile(path: string, data: string | Uint8Array, options?: string | WriteFileOptions): Promise<void> {
  appendFileSync(path, data, options);
}

/** Removes a file. */
export async function unlink(path: string): Promise<void> {
  unlinkSync(path);
}

/** Makes a directory; with { recursive: true } its parents too, and gives the first one it made. */
export async function mkdir(path: string, options?: int | MkdirOptions): Promise<string | undefined> {
  return mkdirSync(path, options);
}

export async function readdir(path: string, options?: string | ReaddirOptions): Promise<string[]> {
  return readdirSync(path, options);
}

/** readdir(path, { withFileTypes: true }): Dirents. */
export async function readdir__withFileTypes(path: string, options: ReaddirOptions): Promise<Dirent[]> {
  return readdirSync(path, { withFileTypes: true, recursive: options.recursive === true });
}

export async function stat(path: string, options?: StatOptions): Promise<Stats> {
  return statSync(path);
}

export async function lstat(path: string, options?: StatOptions): Promise<Stats> {
  return lstatSync(path);
}

export async function rm(path: string, options?: RmOptions): Promise<void> {
  rmSync(path, options);
}

export async function rmdir(path: string, options?: RmdirOptions): Promise<void> {
  rmdirSync(path, options);
}

export async function rename(oldPath: string, newPath: string): Promise<void> {
  renameSync(oldPath, newPath);
}

export async function copyFile(src: string, dest: string, mode: int = 0): Promise<void> {
  copyFileSync(src, dest, mode);
}

export async function cp(src: string, dest: string, options?: CopyOptions): Promise<void> {
  cpSync(src, dest, options);
}

export async function mkdtemp(prefix: string, options: string | null = null): Promise<string> {
  return mkdtempSync(prefix);
}

export async function realpath(path: string, options: string | null = null): Promise<string> {
  return realpathSync(path);
}

export async function access(path: string, mode: int = 0): Promise<void> {
  accessSync(path, mode);
}

export async function chmod(path: string, mode: int | string): Promise<void> {
  chmodSync(path, mode);
}

export async function readlink(path: string, options: string | null = null): Promise<string> {
  return readlinkSync(path);
}

export async function symlink(target: string, path: string, type: string | null = null): Promise<void> {
  symlinkSync(target, path);
}

export async function link(existingPath: string, newPath: string): Promise<void> {
  linkSync(existingPath, newPath);
}

export async function utimes(path: string, atime: number | Date, mtime: number | Date): Promise<void> {
  utimesSync(path, atime, mtime);
}

export async function lutimes(path: string, atime: number | Date, mtime: number | Date): Promise<void> {
  lutimesSync(path, atime, mtime);
}

export async function truncate(path: string, len: int = 0): Promise<void> {
  truncateSync(path, len);
}

/** fs.promises (and require("fs").promises): the functions above, as its static methods. */
export class promises {
  static readFile(path: string, options: ReadFileOptions | null = null): Promise<Buffer> {
    return readFile(path, options);
  }
  static readFile__encoding(path: string, options: string | ReadFileOptions): Promise<string> {
    return readFile__encoding(path, options);
  }
  static writeFile(path: string, data: string | Uint8Array, options?: string | WriteFileOptions): Promise<void> {
    return writeFile(path, data, options);
  }
  static appendFile(path: string, data: string | Uint8Array, options?: string | WriteFileOptions): Promise<void> {
    return appendFile(path, data, options);
  }
  static unlink(path: string): Promise<void> {
    return unlink(path);
  }
  static mkdir(path: string, options?: int | MkdirOptions): Promise<string | undefined> {
    return mkdir(path, options);
  }
  static readdir(path: string, options?: string | ReaddirOptions): Promise<string[]> {
    return readdir(path, options);
  }
  static readdir__withFileTypes(path: string, options: ReaddirOptions): Promise<Dirent[]> {
    return readdir__withFileTypes(path, options);
  }
  static stat(path: string, options?: StatOptions): Promise<Stats> {
    return stat(path, options);
  }
  static lstat(path: string, options?: StatOptions): Promise<Stats> {
    return lstat(path, options);
  }
  static rm(path: string, options?: RmOptions): Promise<void> {
    return rm(path, options);
  }
  static rmdir(path: string, options?: RmdirOptions): Promise<void> {
    return rmdir(path, options);
  }
  static rename(oldPath: string, newPath: string): Promise<void> {
    return rename(oldPath, newPath);
  }
  static copyFile(src: string, dest: string, mode: int = 0): Promise<void> {
    return copyFile(src, dest, mode);
  }
  static cp(src: string, dest: string, options?: CopyOptions): Promise<void> {
    return cp(src, dest, options);
  }
  static mkdtemp(prefix: string): Promise<string> {
    return mkdtemp(prefix);
  }
  static realpath(path: string): Promise<string> {
    return realpath(path);
  }
  static access(path: string, mode: int = 0): Promise<void> {
    return access(path, mode);
  }
  static chmod(path: string, mode: int | string): Promise<void> {
    return chmod(path, mode);
  }
  static readlink(path: string): Promise<string> {
    return readlink(path);
  }
  static symlink(target: string, path: string): Promise<void> {
    return symlink(target, path);
  }
  static utimes(path: string, atime: number | Date, mtime: number | Date): Promise<void> {
    return utimes(path, atime, mtime);
  }
  static truncate(path: string, len: int = 0): Promise<void> {
    return truncate(path, len);
  }
}
