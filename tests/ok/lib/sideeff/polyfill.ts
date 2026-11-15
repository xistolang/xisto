import { mark } from "./registry.ts";
export function unusedHelper(n: number): number {
  return n * 2;
}
export const STAMP = `stamp-${unusedHelper(21)}`;
class Installer {
  static installed = STAMP.length;
}
mark(`polyfill ${STAMP} ${Installer.installed}`);
