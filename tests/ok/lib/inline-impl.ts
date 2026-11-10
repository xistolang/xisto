import type { Collector } from "./inline-iface.ts";
export class Granular implements Collector {
  startRun(e: string, periodo: { inicio: string; fim: string }, by: string): void {
    console.log(e, periodo.inicio, periodo.fim, by);
  }
}
