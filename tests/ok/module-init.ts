// each imported module's top level runs in its own function (XB-176), in import order: plain code, a loop, a try
// build: --split-init
import { registry } from "./lib/modinit/registry.ts";
import { B_DONE } from "./lib/modinit/b.ts";
const keys = [...registry.keys()].join(",");
console.log("main", keys, registry.get("b"), B_DONE);
