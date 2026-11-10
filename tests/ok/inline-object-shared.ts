// an inline object type of primitive fields, written in two files (an interface and the class that implements it): one type
import { Granular } from "./lib/inline-impl.ts";
import type { Collector } from "./lib/inline-iface.ts";
const c: Collector = new Granular();
c.startRun("a", { inicio: "1", fim: "2" }, "x");
