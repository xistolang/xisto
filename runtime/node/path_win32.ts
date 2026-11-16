// xisto:path/win32 (node:path/win32): Windows paths (path.win32) as a module, on any system.

import { win32 } from "xisto:path";
import type { ParsedPath, FormatInputPathObject } from "xisto:path";
export { posix, win32, ParsedPath, FormatInputPathObject } from "xisto:path";

export const sep: string = "\\";
export const delimiter: string = ";";

export function normalize(p: string): string {
  return win32.normalize(p);
}
export function join(...parts: string[]): string {
  return win32.join(...parts);
}
export function resolve(...parts: string[]): string {
  return win32.resolve(...parts);
}
export function relative(from: string, to: string): string {
  return win32.relative(from, to);
}
export function isAbsolute(p: string): boolean {
  return win32.isAbsolute(p);
}
export function dirname(p: string): string {
  return win32.dirname(p);
}
export function basename(p: string, ext: string = ""): string {
  return win32.basename(p, ext);
}
export function extname(p: string): string {
  return win32.extname(p);
}
export function parse(p: string): ParsedPath {
  return win32.parse(p);
}
export function format(p: FormatInputPathObject): string {
  return win32.format(p);
}
export function toNamespacedPath(p: string): string {
  return win32.toNamespacedPath(p);
}
