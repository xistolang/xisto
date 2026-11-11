// A provider given by token to a constructor typed with an interface with methods it does not implement (XB-126):
// TypeScript compares the shape (useExisting: the engine; the worker sees only the minimal contract), so the
// injector wraps the engine in an adapter: wider parameter types, an extra optional parameter, a narrower result
import { Inject, Injectable, Module } from "@nestjs/common";
import { NestFactory } from "@nestjs/core";

export const ENGINE = Symbol("ENGINE");

export interface JobInput {
  company: string;
  from: string;
  dryRun: boolean;
}
export interface JobResult {
  runId: string;
  ok: number;
  message: string | null;
}
export interface Engine {
  execute(input: JobInput, plan: number, opts: { runId: string; quiet?: boolean }): Promise<JobResult>;
}

class ExecuteDto {
  company!: string;
  from!: string;
  dryRun?: boolean;
  trace?: boolean;
}
interface RunOpts {
  runId: string;
  quiet?: boolean;
  onStep?: (n: number) => void;
}
interface FullResult {
  runId: string;
  ok: number;
  message: string | null;
  rows: number[];
}

@Injectable()
class RealEngine {
  private runs = 0;
  async execute(dto: ExecuteDto, plan: number, opts?: RunOpts): Promise<FullResult> {
    this.runs++;
    const quiet = opts?.quiet ?? false;
    return { runId: opts?.runId ?? "none", ok: this.runs, message: quiet ? null : `${dto.company} ${dto.from} plan ${plan} dry ${dto.dryRun}`, rows: [plan] };
  }
  count(): number {
    return this.runs;
  }
}

@Injectable()
class Worker {
  constructor(@Inject(ENGINE) private readonly engine: Engine, private readonly real: RealEngine) {}
  async tick(n: number): Promise<string> {
    const r = await this.engine.execute({ company: "A1", from: "2026-01-01", dryRun: true }, n, { runId: `r${n}`, quiet: n > 1 });
    return `${r.runId} ok=${r.ok} msg=${r.message ?? "-"} count=${this.real.count()}`;
  }
}

@Module({ providers: [RealEngine, { provide: ENGINE, useExisting: RealEngine }, Worker] })
class AppModule {}

async function main(): Promise<void> {
  const app = NestFactory.create(AppModule);
  app.init();
  const w = app.get(Worker) as Worker;
  console.log(await w.tick(1));
  console.log(await w.tick(2));
}
main();
