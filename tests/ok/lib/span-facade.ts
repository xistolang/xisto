// a facade over span-core.ts that declares names the core also has (startSpan, VERSION) and imports the core's under
// other names (XB-224)
import { startSpan as coreStart, VERSION as CORE_VERSION } from "./span-core.ts";
export const VERSION = "facade-1";
export function startSpan(name: string): string {
  return "facade(" + coreStart(name) + ")";
}
export interface Opts {
  startSpan: string;
  VERSION?: string;
}
export class Tracer {
  VERSION = CORE_VERSION;
  startSpan(name: string): string {
    return "tracer(" + startSpan(name) + ")";
  }
  info(): { startSpan: string; VERSION: string } {
    return { startSpan: this.startSpan("i"), VERSION };
  }
}
export function opts(o: Opts): string {
  return o.startSpan + "/" + (o.VERSION ?? "-");
}
