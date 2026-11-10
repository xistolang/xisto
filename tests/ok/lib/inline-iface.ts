export interface Collector {
  startRun(e: string, periodo: { inicio: string; fim: string }, by: string): void;
}
