// reflect-metadata: Reflect.defineMetadata / getMetadata & co by target identity (an object, a function) and property, as on Node
import "reflect-metadata";
const KEY = "oracle_rule";
class Svc { run(): number { return 1; } }
const fn = (x: number): number => x * 2;
const target = new Svc();
Reflect.defineMetadata(KEY, ["a", "b"], target, "run");
Reflect.defineMetadata(KEY, ["c"], fn);
const sym = Symbol("s");
Reflect.defineMetadata(sym, 42, target);
console.log(JSON.stringify(Reflect.getMetadata(KEY, target, "run")), JSON.stringify(Reflect.getMetadata(KEY, fn)));
console.log(Reflect.getMetadata(KEY, target) ?? "none", Reflect.hasMetadata(sym, target), Reflect.getMetadata(sym, target));
const list: string[] = Reflect.getMetadata("nope", fn) ?? [];
console.log(list.length, Reflect.getOwnMetadataKeys(target, "run").length, Reflect.deleteMetadata(KEY, fn), Reflect.hasOwnMetadata(KEY, fn));
