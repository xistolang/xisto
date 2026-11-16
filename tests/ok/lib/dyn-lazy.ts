import { bump } from "./dyn-dep.ts";
console.log("lazy init");
export const LABEL: string = "lazy";
export function twice(n: number): number {
  bump();
  return n * 2;
}
