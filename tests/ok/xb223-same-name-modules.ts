// XB-223: classes and functions with the same name in different modules are different declarations: Span of one module
// is not Span of another, and ns.f is always the f of ns's module
import * as core from "./lib/span-core.ts";
import * as alt from "./lib/span-alt.ts";
import { Span as AltSpan, describe as altDescribe } from "./lib/span-alt.ts";
import { Span as CoreSpan } from "./lib/span-core.ts";
import * as own from "./lib/span-own.ts";
import { run, makeCore } from "./lib/span-own.ts";
import * as own2 from "./lib/span-own2.ts";

// side by side through namespaces
const c = new core.Span("c");
const a = new alt.Span(1, "a");
console.log(core.describe(c));
console.log(alt.describe(a));

// through renamed imports
const c2: CoreSpan = new CoreSpan("c2");
const a2: AltSpan = new AltSpan(2, "a2");
console.log(core.describe(c2), "|", altDescribe(a2), "|", alt.describe(a2));

// a module with its own Span and describe next to the core's
for (const line of run()) console.log(line);
const o = new own.Span(7);
console.log(own.describe(o));
console.log(core.describe(makeCore("made")));

// renamed imports next to the module's own names, statics and constants of the same name
for (const line of own2.run2()) console.log(line);
console.log(own2.describe(own2.Span.make(5)), "|", alt.describe(alt.Span.make(6)));
console.log(alt.TAG, own2.TAG);
for (const s of own2.both()) console.log(s, s instanceof core.Span, s instanceof own2.Span);
const m1 = own2.Span.make(1), m2 = alt.Span.make(2);
console.log(m1.constructor.name, m2.constructor.name, c.constructor.name);

// instanceof between them
const objs: (core.Span | alt.Span | own.Span)[] = [c, a, c2, a2, o];
for (const x of objs) {
  console.log(x instanceof core.Span, x instanceof alt.Span, x instanceof own.Span, x instanceof CoreSpan, x instanceof AltSpan);
}

// JSON and console.log of each
console.log(JSON.stringify(c), JSON.stringify(a), JSON.stringify(o));
console.log(c);
console.log(a);
console.log(o);
const pair: (CoreSpan | AltSpan)[] = [c2, a2];
console.log(pair);
