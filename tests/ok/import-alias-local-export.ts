// XB-194: the local export A next to import { A as B } stays exported (directly and through a barrel), and one class
// imported under two names (one of them an alias, from the module or from its barrel) is the same class
import { type ExecutionContext, local, remote, tools, Tool } from "./lib/alias-export/lib.ts";
import { Tool as BarrelTool } from "./lib/alias-export/index.ts";
import { checkTwo } from "./lib/alias-export/two.ts";
const c: ExecutionContext = local("x");
console.log(c.name, remote().id, remote().kind, tools(), new Tool().label, new BarrelTool().label);
const t: Tool = new BarrelTool();
console.log(t instanceof Tool, checkTwo());
