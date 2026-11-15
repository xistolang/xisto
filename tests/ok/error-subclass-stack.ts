// XB-218: .stack of TypeError, RangeError... (classes that extend Error): read directly, through Error (a parameter,
// `as Error`, an unknown narrowed by instanceof Error), through any, and in a user class that extends TypeError
function line1(s: string | undefined): string { return (s ?? "").split("\n")[0]; }
const t = new TypeError("bad type");
console.log(line1(t.stack), typeof t.stack);
console.log(line1((t as Error).stack));
function viaError(e: Error): string { return line1(e.stack); }
console.log(viaError(new RangeError("bad range")), viaError(new Error("plain")));
function viaUnknown(err: unknown): string { return err instanceof Error ? line1(err.stack) + " / " + err.message : "not an error"; }
console.log(viaUnknown(new SyntaxError("syn")));
console.log(viaUnknown(new Error("e")));
console.log(viaUnknown("text"));
function gen<T extends Error>(e: T): string { return line1(e.stack); }
console.log(gen(new URIError("uri")));
const x: any = new ReferenceError("anyx");
console.log(line1(String(x.stack)), x.message, x.name);
class NotFound extends TypeError { constructor(what: string) { super(`${what} not found`); this.name = "NotFound"; } }
const nf = new NotFound("user");
console.log(line1(nf.stack), viaUnknown(nf));
const errs: Error[] = [t, nf, new Error("last")];
for (const e of errs) console.log(e.name, line1(e.stack));
function thrower(): void { throw new TypeError("thrown"); }
try { thrower(); } catch (e) { console.log(line1((e as Error).stack), (e as Error).stack?.includes("thrower")); }
