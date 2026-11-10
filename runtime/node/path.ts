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
