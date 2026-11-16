export const seen: string[] = [];
export function mark(name: string): void {
  seen.push(name);
  console.log("loaded", name);
}
console.log("registry top");
