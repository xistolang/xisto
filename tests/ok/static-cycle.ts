// XB-145: statics of a class read from a module in an import cycle with the class's module (a function with an inferred
// result, a field initializer, a default parameter, an arrow; statics made by a static method that calls the other module)
import { zero, Holder } from "./lib/cycstatic/util.ts";
import { Dec } from "./lib/cycstatic/dec.ts";
import { firstOf, later } from "./lib/cycstatic/b.ts";
console.log(zero().show(), new Holder().get().show(), Dec.ZERO.plus().show());
console.log(firstOf(), later());
