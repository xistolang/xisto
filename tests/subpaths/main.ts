// deep imports of packages: rxjs/operators and @nestjs/common/interfaces/external/cors-options.interface; Type<T>
import { lastValueFrom, of } from "rxjs";
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
  const services: Type<unknown>[] = [UsersService, OrdersService];
  console.log(services.length);
  const c = corsFor("https://a.example, https://b.example");
  console.log(JSON.stringify(c));
}
main();
