// XB-224: import { a as b } in a module that also declares its own a renames only that top-level a: a class's method
// or field called a, an interface's member a, an object literal's key a and o.a stay a
import { Tracer, startSpan, VERSION, opts } from "./lib/span-facade.ts";
const t = new Tracer();
console.log(t.startSpan("a"), startSpan("b"), VERSION, t.VERSION);
console.log(JSON.stringify(t.info()));
console.log(opts({ startSpan: "s", VERSION: "v" }));
