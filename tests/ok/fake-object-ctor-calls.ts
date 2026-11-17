// An object literal with methods cast to a class (a mock) makes the class used without a `new`: its constructor
// is emitted with the class, so what the constructor calls must be compiled too (register here).
const registry: string[] = [];
function register(name: string): void {
  registry.push(name);
}
class Source {
  name: string;
  constructor(name: string) {
    this.name = name;
    register(name);
  }
  query(sql: string): string {
    return this.name + ": " + sql;
  }
}
const s = { name: "fake", query: (sql: string): string => "fake " + sql } as Source;
console.log(s.query("SELECT 1"), registry.length);
