import { CallHandler, ExecutionContext, Injectable, NestInterceptor } from "@nestjs/common";
import { Reflector } from "@nestjs/core";
import { map, Observable } from "rxjs";
import { RAW_KEY } from "./public.decorator";

export class Envelope {
  success: boolean;
  data: unknown;
  handler: string;
  constructor(data: unknown, handler: string) {
    this.success = true;
    this.data = data;
    this.handler = handler;
  }
}

// every answer as { success, data, handler }, unless the route is @Raw()
@Injectable()
export class TransformInterceptor implements NestInterceptor {
  constructor(private readonly reflector: Reflector) {}

  intercept(context: ExecutionContext, next: CallHandler): Observable<unknown> {
    const raw = this.reflector.get<boolean>(RAW_KEY, context.getHandler());
    if (raw) {
      return next.handle();
    }
    const name = context.getHandler().name;
    return next.handle().pipe(map((data: unknown) => new Envelope(data, name)));
  }
}
