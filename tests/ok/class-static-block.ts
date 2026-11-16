const log: string[] = [];
class Config {
  static a = Config.mk("a");
  static table: Map<string, number> = new Map();
  private static hidden = 3;
  static {
    log.push("block1");
    const base = 10;
    for (let i = 0; i < 3; i++) this.table.set(`k${i}`, base + i);
    Config.table.set("hidden", this.hidden);
  }
  static b = Config.mk("b");
  static {
    log.push(`block2 ${this.b} ${this.table.size}`);
    const f = () => this.a + "!";
    log.push(f());
  }
  static mk(s: string): string { log.push("mk " + s); return s.toUpperCase(); }
  value = 1;
}
console.log(log.join(","));
console.log(Config.table.get("k2"), Config.table.get("hidden"), Config.a, Config.b);
class Empty { static n = 1; static {} }
console.log(new Config().value, Empty.n);
class Box<T> {
  static made = 0;
  static { Box.made = 5; log.push("box"); }
  v: T;
  constructor(v: T) { this.v = v; }
}
console.log(new Box<number>(2).v, Box.made, log.length);
const K = class {
  static tag = "k";
  static { log.push(`anon ${this.tag}`); }
};
console.log(log[log.length - 1]);
