// `import "./m.ts"` with no names still loads the module and runs its top level (XB-184): dependencies first,
// in import order, each module once, also when the same module is imported by name elsewhere
import "./lib/sideeff/polyfill.ts";
import "./lib/sideeff/plugin.ts";
import { seen } from "./lib/sideeff/registry.ts";
import { PLUGIN } from "./lib/sideeff/plugin.ts";
import "./lib/sideeff/polyfill.ts";
console.log("main", seen.join(","), PLUGIN);
