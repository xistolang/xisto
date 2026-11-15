// expect: cannot assign to 'Hooks.version': it is a readonly static field
import { Hooks } from "../ok/lib/static-hooks.ts";
Hooks.version = "2.0";
console.log(Hooks.version);
