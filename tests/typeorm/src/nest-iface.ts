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

// a contract the engine meets only in part (purge it does not have): JavaScript takes the object as it is, and the
// missing method throws when called
interface EngineAdmin {
  execute(input: JobInput, plan: number, opts: { runId: string; quiet?: boolean }): Promise<JobResult>;
  purge(): Promise<void>;
}
@Injectable()
class Admin {
  constructor(@Inject(ENGINE) private readonly engine?: EngineAdmin) {}
  async run(): Promise<string> {
    const e = this.engine;
    if (!e) return "none";
    const r = await e.execute({ company: "B2", from: "2026-02-01", dryRun: false }, 9, { runId: "adm" });
    let purged = "purged";
    try {
      await e.purge();
    } catch (err) {
      purged = "purge threw";
    }
    return `${r.runId} ${r.message ?? "-"} ${purged}`;
  }
}

// the same with a parameter optional in the contract and required in the method, and an enum where a number goes
enum St {
  OK = 0,
  BAD = 1,
}
type InvoiceResult = { status: St; message: string | null };
interface BoletoLike {
  listar(beneficiario?: unknown): Promise<unknown[]>;
  verifica(cd?: number): Promise<{ status: number; message?: string | null }>;
}
@Injectable()
class BoletoReal {
  async verifica(cd: number, ctx?: JobInput): Promise<InvoiceResult> {
    return { status: St.BAD, message: "m" + String(cd) };
  }
}
@Injectable()
class Wizard {
  constructor(@Inject("Boleto") private readonly boleto?: BoletoLike) {}
  async run(): Promise<string> {
    const b = this.boleto;
    if (!b) return "none";
    const r = await b.verifica(5);
    return `status ${r.status} ${r.message ?? "-"}`;
  }
}

@Module({ providers: [RealEngine, { provide: ENGINE, useExisting: RealEngine }, Worker, Admin, BoletoReal, { provide: "Boleto", useExisting: BoletoReal }, Wizard] })
class AppModule {}

async function main(): Promise<void> {
  const app = NestFactory.create(AppModule);
  app.init();
  const w = app.get(Worker) as Worker;
  console.log(await w.tick(1));
  console.log(await w.tick(2));
  console.log(await (app.get(Admin) as Admin).run());
  console.log(await (app.get(Wizard) as Wizard).run());
}
main();
