import { CallHandler, ExecutionContext, Injectable, NestInterceptor } from "@nestjs/common";
import { Response } from "express";
import { finalize, Observable } from "rxjs";
import { AuditLog } from "./audit-log";

// a route-level interceptor: a header on the answer, and a line when the route is done
@Injectable()
export class StampInterceptor implements NestInterceptor {
  constructor(private readonly audit: AuditLog) {}

  intercept(context: ExecutionContext, next: CallHandler): Observable<unknown> {
    context.switchToHttp().getResponse<Response>().setHeader("X-Stamp", "items");
    return next.handle().pipe(finalize(() => this.audit.add("stamp done")));
  }
}
