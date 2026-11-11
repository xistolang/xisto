export const ORIGINAL = 'PK_A';
export function soma(a: number, b: number): number { return a + b; }
function normaliza(s: string): string { return s.trim().toUpperCase(); }
export { normaliza as normalizaTexto };
export interface Cfg { host: string; port: number; }
function makeCfg(name: string, f: () => Cfg): Cfg { const c = f(); return { host: name + ':' + c.host, port: c.port }; }
export default makeCfg('redis', () => ({ host: 'localhost', port: 6379 }));
