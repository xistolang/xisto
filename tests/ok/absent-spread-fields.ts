// m.keys().next().value: the first key; ...(c && { k: v }) and ...(c ? { k: v } : {}) leave k out of JSON when c is false
class Log {
  error(m: string): void { console.log("E " + m); }
  warn(m: string): void { console.log("W " + m); }
}
class Svc {
  private readonly logger = new Log();
  avisar(nivel: "error" | "warn", mensagem: string): void {
    this.logger[nivel](mensagem);
  }
}
new Svc().avisar("warn", "a");
new Svc().avisar("error", "b");
const entries = new Map<string, number>();
entries.set("x", 1); entries.set("y", 2);
const oldest = entries.keys().next().value;
console.log(oldest);
entries.clear();
const none = entries.keys().next().value;
console.log(none == null);
function body(rot?: boolean, mask?: boolean): string {
  return JSON.stringify({ app: "a", ...(rot !== undefined && { rotacionar: rot }), ...(mask !== undefined && { ignorarMascara: mask }) });
}
console.log(body(), body(true), body(false, true));
function body3(rot?: boolean): string {
  return JSON.stringify({ app: "a", ...(rot !== undefined ? { rotacionar: rot } : {}) });
}
function body4(rot: boolean | null): string {
  return JSON.stringify({ app: "a", ...(rot !== null ? { rotacionar: rot } : {}) });
}
console.log(body3(), body3(true), body4(null));
