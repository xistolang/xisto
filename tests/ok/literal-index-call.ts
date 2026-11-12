// obj[k](m) with k a union of string literals: the method of that name (a statement and a value)
class Log {
  error(m: string): void { console.log("E " + m); }
  warn(m: string): void { console.log("W " + m); }
}
function avisar(l: Log, nivel: "error" | "warn", m: string): void {
  l[nivel](m);
}
avisar(new Log(), "error", "b");
avisar(new Log(), "warn", "a");
class Fmt {
  up(s: string): string { return s.toUpperCase(); }
  low(s: string): string { return s.toLowerCase(); }
}
function fmt(f: Fmt, how: "up" | "low", s: string): string { return f[how](s) + "!"; }
console.log(fmt(new Fmt(), "up", "Ab"), fmt(new Fmt(), "low", "Ab"));
