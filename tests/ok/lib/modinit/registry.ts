export const registry: Map<string, number> = new Map<string, number>();
export function register(name: string, n: number): void {
  registry.set(name, n);
}
console.log("registry loaded");
