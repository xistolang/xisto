// xisto:path: Node's path module (POSIX), written in xisto. Same results as Node's path.posix.

export const sep: string = "/";
export const delimiter: string = ":";

// a path without "." and "..", and with single slashes; leading ".." kept for a relative path
function normalizeString(p: string, absolute: boolean): string {
  const out: string[] = [];
  for (const part of p.split("/")) {
    if (part.length === 0 || part === ".") {
      continue;
    }
    if (part === "..") {
      if (out.length > 0 && out[out.length - 1] !== "..") {
        out.pop();
      } else if (!absolute) {
        out.push("..");
      }
      continue;
    }
    out.push(part);
  }
  return out.join("/");
}

export function isAbsolute(p: string): boolean {
  return p.startsWith("/");
}

export function normalize(p: string): string {
  if (p.length === 0) {
    return ".";
  }
  const absolute = p.startsWith("/");
  const trailing = p.endsWith("/");
  let s = normalizeString(p, absolute);
  if (s.length === 0 && !absolute) {
    s = ".";
  }
  if (s.length > 0 && trailing) {
    s += "/";
  }
  return absolute ? "/" + s : s;
}

export function join(...parts: string[]): string {
  const kept: string[] = [];
  for (const part of parts) {
    if (part.length > 0) {
      kept.push(part);
    }
  }
  if (kept.length === 0) {
    return ".";
  }
  return normalize(kept.join("/"));
}

export function resolve(...parts: string[]): string {
  let resolved = "";
  let absolute = false;
  for (let i = parts.length - 1; i >= -1 && !absolute; i--) {
    const part = i >= 0 ? parts[i] : process.cwd();
    if (part.length === 0) {
      continue;
    }
    resolved = resolved.length > 0 ? part + "/" + resolved : part;
    absolute = part.startsWith("/");
  }
  const s = normalizeString(resolved, absolute);
  if (absolute) {
    return "/" + s;
  }
  return s.length > 0 ? s : ".";
}

export function dirname(p: string): string {
  if (p.length === 0) {
    return ".";
  }
  let end = p.length;
  while (end > 1 && p.slice(end - 1, end) === "/") {
    end--;
  }
  const cut = p.slice(0, end).lastIndexOf("/");
  if (cut < 0) {
    return ".";
  }
  if (cut === 0) {
    return "/";
  }
  let d = cut;
  while (d > 1 && p.slice(d - 1, d) === "/") {
    d--;
  }
  return p.slice(0, d);
}

export function basename(p: string, ext: string = ""): string {
  let end = p.length;
  while (end > 0 && p.slice(end - 1, end) === "/") {
    end--;
  }
  const s = p.slice(0, end);
  let base = s.slice(s.lastIndexOf("/") + 1);
  if (ext.length > 0 && base.endsWith(ext) && base !== ext) {
    base = base.slice(0, base.length - ext.length);
  }
  return base;
}

export function extname(p: string): string {
  const base = basename(p);
  const dot = base.lastIndexOf(".");
  if (dot <= 0 || base === "..") {
    return "";
  }
  return base.slice(dot);
}

export function relative(from: string, to: string): string {
  const a = resolve(from).split("/").filter((s: string): boolean => s.length > 0);
  const b = resolve(to).split("/").filter((s: string): boolean => s.length > 0);
  let i = 0;
  while (i < a.length && i < b.length && a[i] === b[i]) {
    i++;
  }
  const up: string[] = [];
  for (let k = i; k < a.length; k++) {
    up.push("..");
  }
  for (let k = i; k < b.length; k++) {
    up.push(b[k]);
  }
  return up.join("/");
}

/** What path.parse gives, and path.format takes (each part optional there). */
export interface ParsedPath {
  root: string;
  dir: string;
  base: string;
  ext: string;
  name: string;
}
export interface FormatInputPathObject {
  root?: string;
  dir?: string;
  base?: string;
  ext?: string;
  name?: string;
}

function isSep(c: string, win: boolean): boolean {
  return c === "/" || (win && c === "\\");
}

function isDriveLetter(c: string): boolean {
  return (c >= "A" && c <= "Z") || (c >= "a" && c <= "z");
}

// the base, name and ext of path from start on (Node's parse loop): into r, and where the last part starts
function parseTail(path: string, start: int, win: boolean, r: ParsedPath): int {
  let startDot = -1;
  let startPart = start;
  let end = -1;
  let matchedSlash = true;
  let preDotState = 0;
  for (let i = path.length - 1; i >= start; i--) {
    const c = path.charAt(i);
    if (isSep(c, win)) {
      if (!matchedSlash) {
        startPart = i + 1;
        break;
      }
      continue;
    }
    if (end === -1) {
      matchedSlash = false;
      end = i + 1;
    }
    if (c === ".") {
      if (startDot === -1) startDot = i;
      else if (preDotState !== 1) preDotState = 1;
    } else if (startDot !== -1) {
      preDotState = -1;
    }
  }
  if (end !== -1) {
    if (startDot === -1 || preDotState === 0 || (preDotState === 1 && startDot === end - 1 && startDot === startPart + 1)) {
      r.base = path.slice(startPart, end);
      r.name = r.base;
    } else {
      r.name = path.slice(startPart, startDot);
      r.base = path.slice(startPart, end);
      r.ext = path.slice(startDot, end);
    }
  }
  return startPart;
}

/** The parts of a path: { root, dir, base, ext, name }. */
export function parse(path: string): ParsedPath {
  const r: ParsedPath = { root: "", dir: "", base: "", ext: "", name: "" };
  if (path.length === 0) return r;
  const absolute = path.startsWith("/");
  if (absolute) r.root = "/";
  const startPart = parseTail(path, absolute ? 1 : 0, false, r);
  if (startPart > (absolute ? 1 : 0)) r.dir = path.slice(0, startPart - 1);
  else if (absolute) r.dir = "/";
  return r;
}

function formatWith(sep: string, p: FormatInputPathObject): string {
  const root = p.root ?? "";
  const d = p.dir ?? "";
  const dir = d.length > 0 ? d : root;
  const ext = p.ext ?? "";
  const b = p.base ?? "";
  const base = b.length > 0 ? b : (p.name ?? "") + (ext.length > 0 && !ext.startsWith(".") ? "." + ext : ext);
  if (dir.length === 0) return base;
  return dir === root ? dir + base : dir + sep + base;
}

/** The path of its parts (path.parse's inverse): dir (or root) and base (or name and ext). */
export function format(p: FormatInputPathObject): string {
  return formatWith("/", p);
}

/** On POSIX, the path itself. */
export function toNamespacedPath(p: string): string {
  return p;
}

/** Node's path.posix: the same functions (this module is POSIX). */
export class posix {
  static readonly sep: string = "/";
  static readonly delimiter: string = ":";
  static normalize(p: string): string {
    return normalize(p);
  }
  static join(...parts: string[]): string {
    return join(...parts);
  }
  static resolve(...parts: string[]): string {
    return resolve(...parts);
  }
  static isAbsolute(p: string): boolean {
    return isAbsolute(p);
  }
  static dirname(p: string): string {
    return dirname(p);
  }
  static basename(p: string, ext: string = ""): string {
    return basename(p, ext);
  }
  static extname(p: string): string {
    return extname(p);
  }
  static relative(from: string, to: string): string {
    return relative(from, to);
  }
  static parse(p: string): ParsedPath {
    return parse(p);
  }
  static format(p: FormatInputPathObject): string {
    return format(p);
  }
  static toNamespacedPath(p: string): string {
    return p;
  }
}

// ---- path.win32: Node's Windows paths (drive letters, UNC roots, both separators) ----

// Node's normalizeString: "." and ".." of path resolved, its separators made sep
function normalizeParts(path: string, allowAboveRoot: boolean, sep: string, win: boolean): string {
  let res = "";
  let lastSegmentLength = 0;
  let lastSlash = -1;
  let dots = 0;
  let c = "";
  for (let i = 0; i <= path.length; i++) {
    if (i < path.length) c = path.charAt(i);
    else if (isSep(c, win)) break;
    else c = "/";
    if (isSep(c, win)) {
      if (lastSlash === i - 1 || dots === 1) {
        // (nothing)
      } else if (dots === 2) {
        if (res.length < 2 || lastSegmentLength !== 2 || !res.endsWith("..")) {
          if (res.length > 2) {
            const k = res.lastIndexOf(sep);
            if (k === -1) {
              res = "";
              lastSegmentLength = 0;
            } else {
              res = res.slice(0, k);
              lastSegmentLength = res.length - 1 - res.lastIndexOf(sep);
            }
            lastSlash = i;
            dots = 0;
            continue;
          } else if (res.length !== 0) {
            res = "";
            lastSegmentLength = 0;
            lastSlash = i;
            dots = 0;
            continue;
          }
        }
        if (allowAboveRoot) {
          res += res.length > 0 ? sep + ".." : "..";
          lastSegmentLength = 2;
        }
      } else {
        if (res.length > 0) res += sep + path.slice(lastSlash + 1, i);
        else res = path.slice(lastSlash + 1, i);
        lastSegmentLength = i - lastSlash - 1;
      }
      lastSlash = i;
      dots = 0;
    } else if (c === "." && dots !== -1) {
      dots++;
    } else {
      dots = -1;
    }
  }
  return res;
}

// the end of a UNC root \\server\share at the start of path (after its separator), or -1; whole: it is all of path
function uncRootEnd(path: string): int {
  const len = path.length;
  let j = 2;
  let last = j;
  while (j < len && !isSep(path.charAt(j), true)) j++;
  if (j >= len || j === last) return -1;
  last = j;
  while (j < len && isSep(path.charAt(j), true)) j++;
  if (j >= len || j === last) return -1;
  last = j;
  while (j < len && !isSep(path.charAt(j), true)) j++;
  if (j === len) return len;
  return j !== last ? j : -1;
}

function winNormalize(path: string): string {
  const len = path.length;
  if (len === 0) return ".";
  let rootEnd = 0;
  let device = "";
  let hasDevice = false;
  let absolute = false;
  const c = path.charAt(0);
  if (len === 1) return c === "/" ? "\\" : path;
  if (isSep(c, true)) {
    absolute = true;
    if (isSep(path.charAt(1), true)) {
      const u = uncRootEnd(path);
      if (u === len) {
        const parts = path.slice(2).split(/[\\/]+/);
        return `\\\\${parts[0]}\\${parts[1]}\\`;
      }
      if (u > 0) {
        const parts = path.slice(2, u).split(/[\\/]+/);
        device = `\\\\${parts[0]}\\${parts[1]}`;
        hasDevice = true;
        rootEnd = u;
      }
    } else {
      rootEnd = 1;
    }
  } else if (isDriveLetter(c) && path.charAt(1) === ":") {
    device = path.slice(0, 2);
    hasDevice = true;
    rootEnd = 2;
    if (len > 2 && isSep(path.charAt(2), true)) {
      absolute = true;
      rootEnd = 3;
    }
  }
  let tail = rootEnd < len ? normalizeParts(path.slice(rootEnd), !absolute, "\\", true) : "";
  if (tail.length === 0 && !absolute) tail = ".";
  if (tail.length > 0 && isSep(path.charAt(len - 1), true)) tail += "\\";
  if (!hasDevice) return absolute ? "\\" + tail : tail;
  return absolute ? device + "\\" + tail : device + tail;
}

function winIsAbsolute(path: string): boolean {
  const len = path.length;
  if (len === 0) return false;
  const c = path.charAt(0);
  return isSep(c, true) || (len > 2 && isDriveLetter(c) && path.charAt(1) === ":" && isSep(path.charAt(2), true));
}

function winJoin(parts: string[]): string {
  let joined = "";
  let first = "";
  let any = false;
  for (const p of parts) {
    if (p.length === 0) continue;
    if (!any) {
      joined = p;
      first = p;
      any = true;
    } else {
      joined += "\\" + p;
    }
  }
  if (!any) return ".";
  let needsReplace = true;
  let slashCount = 0;
  if (isSep(first.charAt(0), true)) {
    slashCount++;
    if (first.length > 1 && isSep(first.charAt(1), true)) {
      slashCount++;
      if (first.length > 2) {
        if (isSep(first.charAt(2), true)) slashCount++;
        else needsReplace = false;
      }
    }
  }
  if (needsReplace) {
    while (slashCount < joined.length && isSep(joined.charAt(slashCount), true)) slashCount++;
    if (slashCount >= 2) joined = "\\" + joined.slice(slashCount);
  }
  return winNormalize(joined);
}

// where the root of a Windows path ends (C:\ 3, C: 2, \ 1, \\server\share\ after it), 0 for none
function winRootEnd(path: string): int {
  const len = path.length;
  if (len === 0) return 0;
  const c = path.charAt(0);
  if (isSep(c, true)) {
    if (len > 1 && isSep(path.charAt(1), true)) {
      const u = uncRootEnd(path);
      if (u === len) return len;
      if (u > 0) return u + 1;
    }
    return 1;
  }
  if (isDriveLetter(c) && len > 1 && path.charAt(1) === ":") return len > 2 && isSep(path.charAt(2), true) ? 3 : 2;
  return 0;
}

function winDirname(path: string): string {
  const len = path.length;
  if (len === 0) return ".";
  if (len === 1) return isSep(path, true) ? path : ".";
  let rootEnd = winRootEnd(path);
  if (rootEnd === len && len > 3 && isSep(path.charAt(1), true)) return path;
  const offset = rootEnd;
  if (rootEnd === 0) rootEnd = -1;
  let end = -1;
  let matchedSlash = true;
  for (let i = len - 1; i >= offset; i--) {
    if (isSep(path.charAt(i), true)) {
      if (!matchedSlash) {
        end = i;
        break;
      }
    } else {
      matchedSlash = false;
    }
  }
  if (end === -1) {
    if (rootEnd === -1) return ".";
    end = rootEnd;
  }
  return path.slice(0, end);
}

function winBasename(path: string, suffix: string): string {
  let start = 0;
  let end = -1;
  let matchedSlash = true;
  if (path.length >= 2 && isDriveLetter(path.charAt(0)) && path.charAt(1) === ":") start = 2;
  if (suffix.length > 0 && suffix.length <= path.length) {
    if (suffix === path) return "";
    let extIdx = suffix.length - 1;
    let firstNonSlashEnd = -1;
    for (let i = path.length - 1; i >= start; i--) {
      const c = path.charAt(i);
      if (isSep(c, true)) {
        if (!matchedSlash) {
          start = i + 1;
          break;
        }
      } else {
        if (firstNonSlashEnd === -1) {
          matchedSlash = false;
          firstNonSlashEnd = i + 1;
        }
        if (extIdx >= 0) {
          if (c === suffix.charAt(extIdx)) {
            extIdx--;
            if (extIdx === -1) end = i;
          } else {
            extIdx = -1;
            end = firstNonSlashEnd;
          }
        }
      }
    }
    if (start === end) end = firstNonSlashEnd;
    else if (end === -1) end = path.length;
    return path.slice(start, end);
  }
  for (let i = path.length - 1; i >= start; i--) {
    if (isSep(path.charAt(i), true)) {
      if (!matchedSlash) {
        start = i + 1;
        break;
      }
    } else if (end === -1) {
      matchedSlash = false;
      end = i + 1;
    }
  }
  return end === -1 ? "" : path.slice(start, end);
}

function winParse(path: string): ParsedPath {
  const r: ParsedPath = { root: "", dir: "", base: "", ext: "", name: "" };
  const len = path.length;
  if (len === 0) return r;
  if (len === 1) {
    if (isSep(path, true)) {
      r.root = path;
      r.dir = path;
    } else {
      r.base = path;
      r.name = path;
    }
    return r;
  }
  const rootEnd = winRootEnd(path);
  if (rootEnd === len && (len <= 3 || !isSep(path.charAt(1), true)) && !(len === 1 && !isSep(path, true))) {
    if (isDriveLetter(path.charAt(0)) && path.charAt(1) === ":") {
      r.root = path;
      r.dir = path;
      return r;
    }
  }
  if (rootEnd > 0) r.root = path.slice(0, rootEnd);
  const startPart = parseTail(path, rootEnd, true, r);
  r.dir = startPart > 0 && startPart !== rootEnd ? path.slice(0, startPart - 1) : r.root;
  return r;
}

// Node's win32.resolve: right to left until an absolute path on a known device; the current directory (its / made \)
// fills in. (Node also reads the per-drive directory of the "=C:" variables Windows sets; on POSIX there are none.)
function winResolve(args: string[]): string {
  let resolvedDevice = "";
  let resolvedTail = "";
  let resolvedAbsolute = false;
  for (let i = args.length - 1; i >= -1; i--) {
    let path = "";
    if (i >= 0) {
      path = args[i];
      if (path.length === 0) continue;
    } else if (resolvedDevice.length === 0) {
      path = process.cwd();
      if (args.length === 0 || (args.length === 1 && (args[0] === "" || args[0] === ".") && isSep(path.charAt(0), true))) {
        return path.replaceAll("/", "\\");
      }
    } else {
      path = process.cwd();
      if (path.slice(0, 2).toLowerCase() !== resolvedDevice.toLowerCase() && path.charAt(2) === "\\") path = resolvedDevice + "\\";
    }
    const len = path.length;
    let rootEnd = 0;
    let device = "";
    let absolute = false;
    const c = path.charAt(0);
    if (len === 1) {
      if (isSep(c, true)) {
        rootEnd = 1;
        absolute = true;
      }
    } else if (isSep(c, true)) {
      absolute = true;
      if (isSep(path.charAt(1), true)) {
        let j = 2;
        let last = j;
        while (j < len && !isSep(path.charAt(j), true)) j++;
        if (j < len && j !== last) {
          const firstPart = path.slice(last, j);
          last = j;
          while (j < len && isSep(path.charAt(j), true)) j++;
          if (j < len && j !== last) {
            last = j;
            while (j < len && !isSep(path.charAt(j), true)) j++;
            if (j === len || j !== last) {
              if (firstPart !== "." && firstPart !== "?") {
                device = "\\\\" + firstPart + "\\" + path.slice(last, j);
                rootEnd = j;
              } else {
                device = "\\\\" + firstPart;
                rootEnd = 4;
              }
            }
          }
        }
      } else {
        rootEnd = 1;
      }
    } else if (isDriveLetter(c) && path.charAt(1) === ":") {
      device = path.slice(0, 2);
      rootEnd = 2;
      if (len > 2 && isSep(path.charAt(2), true)) {
        absolute = true;
        rootEnd = 3;
      }
    }
    if (device.length > 0) {
      if (resolvedDevice.length > 0) {
        if (device.toLowerCase() !== resolvedDevice.toLowerCase()) continue;
      } else {
        resolvedDevice = device;
      }
    }
    if (resolvedAbsolute) {
      if (resolvedDevice.length > 0) break;
    } else {
      resolvedTail = path.slice(rootEnd) + "\\" + resolvedTail;
      resolvedAbsolute = absolute;
      if (absolute && resolvedDevice.length > 0) break;
    }
  }
  resolvedTail = normalizeParts(resolvedTail, !resolvedAbsolute, "\\", true);
  if (resolvedAbsolute) return resolvedDevice + "\\" + resolvedTail;
  const r = resolvedDevice + resolvedTail;
  return r.length > 0 ? r : ".";
}

// Node's win32.relative: case-insensitive, over the resolved paths
function winRelative(fromIn: string, toIn: string): string {
  if (fromIn === toIn) return "";
  const fromOrig = winResolve([fromIn]);
  const toOrig = winResolve([toIn]);
  if (fromOrig === toOrig) return "";
  const from = fromOrig.toLowerCase();
  const to = toOrig.toLowerCase();
  if (from === to) return "";
  if (fromOrig.length !== from.length || toOrig.length !== to.length) {
    const fromSplit = fromOrig.split("\\");
    const toSplit = toOrig.split("\\");
    if (fromSplit[fromSplit.length - 1] === "") fromSplit.pop();
    if (toSplit[toSplit.length - 1] === "") toSplit.pop();
    const fromLen = fromSplit.length;
    const toLen = toSplit.length;
    const length = fromLen < toLen ? fromLen : toLen;
    let i = 0;
    for (; i < length; i++) {
      if (fromSplit[i].toLowerCase() !== toSplit[i].toLowerCase()) break;
    }
    if (i === 0) return toOrig;
    if (i === length) {
      if (toLen > length) return toSplit.slice(i).join("\\");
      if (fromLen > length) return "..\\".repeat(fromLen - 1 - i) + "..";
      return "";
    }
    return "..\\".repeat(fromLen - i) + toSplit.slice(i).join("\\");
  }
  let fromStart = 0;
  while (fromStart < from.length && from.charAt(fromStart) === "\\") fromStart++;
  let fromEnd = from.length;
  while (fromEnd - 1 > fromStart && from.charAt(fromEnd - 1) === "\\") fromEnd--;
  const fromLen = fromEnd - fromStart;
  let toStart = 0;
  while (toStart < to.length && to.charAt(toStart) === "\\") toStart++;
  let toEnd = to.length;
  while (toEnd - 1 > toStart && to.charAt(toEnd - 1) === "\\") toEnd--;
  const toLen = toEnd - toStart;
  const length = fromLen < toLen ? fromLen : toLen;
  let lastCommonSep = -1;
  let i = 0;
  for (; i < length; i++) {
    const fc = from.charAt(fromStart + i);
    if (fc !== to.charAt(toStart + i)) break;
    else if (fc === "\\") lastCommonSep = i;
  }
  if (i !== length) {
    if (lastCommonSep === -1) return toOrig;
  } else {
    if (toLen > length) {
      if (to.charAt(toStart + i) === "\\") return toOrig.slice(toStart + i + 1);
      if (i === 2) return toOrig.slice(toStart + i);
    }
    if (fromLen > length) {
      if (from.charAt(fromStart + i) === "\\") lastCommonSep = i;
      else if (i === 2) lastCommonSep = 3;
    }
    if (lastCommonSep === -1) lastCommonSep = 0;
  }
  let out = "";
  for (i = fromStart + lastCommonSep + 1; i <= fromEnd; ++i) {
    if (i === fromEnd || from.charAt(i) === "\\") out += out.length === 0 ? ".." : "\\..";
  }
  toStart += lastCommonSep;
  if (out.length > 0) return out + toOrig.slice(toStart, toEnd);
  if (toOrig.charAt(toStart) === "\\") ++toStart;
  return toOrig.slice(toStart, toEnd);
}

/** Node's path.win32: Windows paths (C:\ and \\server\share roots, \ and / separators), on any system. */
export class win32 {
  static readonly sep: string = "\\";
  static readonly delimiter: string = ";";
  static normalize(p: string): string {
    return winNormalize(p);
  }
  static join(...parts: string[]): string {
    return winJoin(parts);
  }
  static resolve(...parts: string[]): string {
    return winResolve(parts);
  }
  static relative(from: string, to: string): string {
    return winRelative(from, to);
  }
  static isAbsolute(p: string): boolean {
    return winIsAbsolute(p);
  }
  static dirname(p: string): string {
    return winDirname(p);
  }
  static basename(p: string, ext: string = ""): string {
    return winBasename(p, ext);
  }
  static extname(p: string): string {
    const r = winParse(p);
    return r.ext;
  }
  static parse(p: string): ParsedPath {
    return winParse(p);
  }
  static format(p: FormatInputPathObject): string {
    return formatWith("\\", p);
  }
  /** C:\x as \\?\C:\x, \\server\share\x as \\?\UNC\server\share\x, for an absolute path (a relative one is
   * given back as it is: Node resolves it against the current directory first). */
  static toNamespacedPath(p: string): string {
    if (!winIsAbsolute(p)) return p;
    let r = winNormalize(p);
    if (r.length > winRootEnd(r) && r.endsWith("\\")) r = r.slice(0, r.length - 1);
    if (r.length <= 2) return p;
    if (r.startsWith("\\\\")) {
      const c = r.charAt(2);
      return c !== "?" && c !== "." ? "\\\\?\\UNC\\" + r.slice(2) : r;
    }
    if (isDriveLetter(r.charAt(0)) && r.charAt(1) === ":" && r.charAt(2) === "\\") return "\\\\?\\" + r;
    return r;
  }
}
