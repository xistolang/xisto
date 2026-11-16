// XB-184 with each module's top level in its own function (--split-init): an import for effects still runs it, once
// build: --split-init
import "./lib/sideeff/plugin.ts";
import { seen } from "./lib/sideeff/registry.ts";
import "./lib/sideeff/polyfill.ts";
console.log("main", seen.join(","));
