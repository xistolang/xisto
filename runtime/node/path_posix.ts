// xisto:path/posix (node:path/posix): POSIX paths, the same module as xisto:path (path.posix).

export const sep: string = "/";
export const delimiter: string = ":";

export {
  normalize,
  join,
  resolve,
  isAbsolute,
  dirname,
  basename,
  extname,
  relative,
  parse,
  format,
  toNamespacedPath,
  posix,
  win32,
  ParsedPath,
  FormatInputPathObject,
} from "xisto:path";
