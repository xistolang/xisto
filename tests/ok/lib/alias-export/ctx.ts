export interface ExecutionContext { id: number; kind: string; }
export function makeCtx(id: number): ExecutionContext { return { id, kind: "remote" }; }
export class Tool { name = "remote-tool"; }
