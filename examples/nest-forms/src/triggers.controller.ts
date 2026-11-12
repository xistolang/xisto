import "reflect-metadata";
import { CallHandler, Controller, ExecutionContext, Get, Injectable, NestInterceptor, SetMetadata, UseInterceptors } from "@nestjs/common";
import { Reflector } from "@nestjs/core";
import { Observable, map } from "rxjs";

export const TRIGGERS_KEY = "oracle_triggers";
interface TriggerMeta {
  name: string;
}

// a TypeScript-style method decorator that reads what the previous one recorded on descriptor.value and records the
// longer list through Nest's SetMetadata (Nest writes it there with reflect-metadata)
export function Trigger(name: string): MethodDecorator {
  return (target, propertyKey, descriptor) => {
    const existing: TriggerMeta[] = Reflect.getMetadata(TRIGGERS_KEY, descriptor.value as object) ?? [];
    existing.push({ name });
    SetMetadata(TRIGGERS_KEY, existing)(target, propertyKey, descriptor);
  };
}

@Injectable()
export class TriggerInterceptor implements NestInterceptor {
  constructor(private readonly reflector: Reflector) {}
  intercept(context: ExecutionContext, next: CallHandler): Observable<unknown> {
    const list = this.reflector.get<TriggerMeta[]>(TRIGGERS_KEY, context.getHandler()) ?? [];
    const screen = Reflect.getMetadata("screen_code", context.getClass());
    return next.handle().pipe(map((body) => ({ body, triggers: list.map((t) => t.name), screen: screen ?? null })));
  }
}

@SetMetadata("screen_code", "T2200")
@Controller("triggers")
@UseInterceptors(TriggerInterceptor)
export class TriggersController {
  @Trigger("TRG_A")
  @Trigger("TRG_B")
  @Get()
  run(): string {
    return "ok";
  }
}
