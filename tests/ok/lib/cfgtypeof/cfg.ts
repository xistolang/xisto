export interface Opt { name: string; port: number; }
export function registerAs<T>(key: string, f: () => T): () => T { return f; }
export const dbs = registerAs('databases', (): Opt[] => [{ name: 'D', port: 1 }]);
