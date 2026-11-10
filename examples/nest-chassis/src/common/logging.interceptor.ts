import { CallHandler, ExecutionContext, Injectable, NestInterceptor } from "@nestjs/common";
import { Request } from "express";
import { catchError, Observable, tap, throwError } from "rxjs";
import { AuditLog } from "./audit-log";

// one line before the route, one after (or on its error), with the controller and the handler
@Injectable()
export class LoggingInterceptor implements NestInterceptor {
  constructor(private readonly audit: AuditLog) {}

  intercept(context: ExecutionContext, next: CallHandler): Observable<unknown> {
    const req = context.switchToHttp().getRequest<Request>();
    const where = `${context.getClass().name}.${context.getHandler().name}`;
    this.audit.add(`-> ${req.method} ${where}`);
    return next.handle().pipe(
      tap(() => this.audit.add(`<- ${where} ok`)),
      catchError((err: Error) => {
        this.audit.add(`<- ${where} ${err.name}: ${err.message}`);
        return throwError(() => err);
      }),
    );
  }
}
