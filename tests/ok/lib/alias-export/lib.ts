// XB-194: a module importing { A as B } while declaring and exporting its own A
import type { ExecutionContext as RemoteCtx } from "./ctx.ts";
import { makeCtx, Tool as RemoteTool } from "./ctx.ts";
export interface ExecutionContext { name: string; }
export class Tool { label = "local-tool"; }
export function local(n: string): ExecutionContext { return { name: n }; }
export function remote(): RemoteCtx { return makeCtx(7); }
export function tools(): string { return `${new Tool().label} ${new RemoteTool().name}`; }
