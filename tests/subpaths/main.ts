// deep imports of packages: rxjs/operators and @nestjs/common/interfaces/external/cors-options.interface; Type<T>
import { catchError, lastValueFrom, of, throwError } from "rxjs";
import { finalize, map, tap } from "rxjs/operators";
import { Injectable, Type } from "@nestjs/common";
import type { CorsOptions } from "@nestjs/common/interfaces/external/cors-options.interface";

@Injectable()
class UsersService {}

@Injectable()
class OrdersService {}

function corsFor(origins: string): CorsOptions {
  return {
    origin: origins.split(",").map((o: string) => o.trim()),
    methods: ["GET", "POST"],
    credentials: true,
    allowedHeaders: ["Content-Type", "Authorization"],
    maxAge: 600,
  };
}

// dd-trace loaded as an optional package (require in a try), its trace tested before use and called with a callback
interface SpanLite {
  setTag(key: string, value: unknown): void;
}
interface TracerLite {
  trace?: <T>(name: string, opts: { resource: string; type: string; tags: Record<string, unknown> }, fn: (span: SpanLite) => T) => T;
}
let tracer: TracerLite | null;
try {
  tracer = require("dd-trace") as TracerLite;
} catch {
  tracer = null;
}
let anyTracer: any;
try {
  anyTracer = require("dd-trace");
} catch {
  anyTracer = null;
}
function withSpan<T>(name: string, fn: (span: SpanLite | null) => Promise<T>): Promise<T> {
  if (!tracer?.trace) return fn(null);
  return tracer.trace(name, { resource: "job", type: "worker", tags: { n: 1 } }, (span) => fn(span));
}
function traced(n: number): number {
  if (!anyTracer?.trace) return -1;
  return anyTracer.trace("double", { type: "app", tags: {} }, () => n * 2);
}

async function main(): Promise<void> {
  const seen: string[] = [];
  const v = await lastValueFrom(
    of(20).pipe(
      map((x: unknown) => (x as number) + 1),
      tap((x: unknown) => {
        seen.push("tap " + String(x));
      }),
      tap(() => {
        seen.push("tap without the value");
      }),
      finalize(() => {
        seen.push("finalize");
      }),
    ),
  );
  console.log(v, seen.join(", "));
  // tap with an observer: next, error and complete, in rxjs's order
  const log: string[] = [];
  const w = await lastValueFrom(
    of(6).pipe(
      tap({ next: (x: unknown) => log.push("next " + String(x)), error: (e: Error) => log.push("error " + e.message), complete: () => log.push("complete") }),
      map((x: unknown) => (x as number) * 2),
    ),
  );
  const z = await lastValueFrom(
    throwError(() => new Error("boom")).pipe(
      tap({ next: () => log.push("never"), error: (e: Error) => log.push("error " + e.message) }),
      catchError((e: Error) => of("caught " + e.message)),
    ),
  );
  console.log(w, z, log.join(" | "));
  const spanned = await withSpan("job", async (span: SpanLite | null): Promise<string> => {
    span?.setTag("k", 1);
    return span === null ? "no span" : "in a span";
  });
  console.log(spanned, traced(21));
  const services: Type<unknown>[] = [UsersService, OrdersService];
  console.log(services.length);
  const c = corsFor("https://a.example, https://b.example");
  console.log(JSON.stringify(c));
}
main();
