// static fields of a class assigned from another module, as in TypeScript (XB-228): a function-typed field (an
// APM's keepTrace hook), a nullable callback, a number (=, +=, ++), a string and an inferred number given a
// fraction, also through a namespace import; the class's own methods see the new values
import { Hooks } from "./lib/static-hooks.ts";
import * as hooks from "./lib/static-hooks.ts";

console.log(Hooks.check("a"), Hooks.check("abcd"));
Hooks.keepTrace = (name: string) => name.length > 3;
console.log(Hooks.check("a"), Hooks.check("abcd"));

Hooks.onEnd = (n: number) => console.log("ended with", n);
Hooks.finish(2);
Hooks.count = 10;
Hooks.count += 5;
Hooks.count++;
Hooks.finish(1);

Hooks.label = "custom";
console.log(Hooks.label, Hooks.version, Hooks.count);

Hooks.ratio = 0.25;
hooks.Hooks.ratio += 0.5;
hooks.Hooks.keepTrace = (name: string) => name === "x";
console.log(Hooks.ratio, hooks.Hooks.check("x"), Hooks.check("y"), Hooks.keepTrace("x"));
