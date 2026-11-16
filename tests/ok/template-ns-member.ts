// ${ns.x} with ns an `import * as` namespace, ${x instanceof C}, ${alias(..)} of an import renamed: read inside ${}
// as outside it (XB-235)
import * as path from "node:path";
import { join as pjoin } from "node:path";
class A { a = 1; }
const a: unknown = new A();
console.log(`${path.sep}|${path.basename("/x/y.ts")}|${a instanceof A}|${pjoin("p", "q")}`);
class K { n = 3; show(): string { return `${this.n} ${this instanceof K} ${path.extname("f.md")}`; } }
console.log(new K().show());
