// ExecutionContext and RouteHandler, apart from index.ts: index declares the @Request() decorator, which would
// clash there with Express's Request class.
import { ClassInfo } from "xisto:reflect";
import { Request, Response } from "express";

/**
 * The route method a request runs, as ExecutionContext.getHandler() gives it (Reflector reads its metadata).
 * Node: the method's function; here its class, its index among the class's methods and its name.
 */
export class RouteHandler {
  readonly cls: ClassInfo;
  readonly index: int;
  readonly name: string;
  constructor(cls: ClassInfo, index: int, name: string) {
    this.cls = cls;
    this.index = index;
    this.name = name;
  }
}

/**
 * What a guard, an interceptor, a filter or a custom parameter decorator sees of the current request.
 * @example ctx.switchToHttp().getRequest().headers.authorization
 */
export class ExecutionContext {
  private readonly request: Request;
  private readonly response: Response;
  private handler: RouteHandler | null = null;
  constructor(request: Request, response: Response) {
    this.request = request;
    this.response = response;
  }
  switchToHttp(): ExecutionContext {
    return this;
  }
  getRequest<T = Request>(): T {
    return this.request as unknown as T;
  }
  getResponse<T = Response>(): T {
    return this.response as unknown as T;
  }
  /** "http": the kind of request (Nest also has "rpc" and "ws"). */
  getType(): string {
    return "http";
  }
  /** The route method that handles the request. */
  getHandler(): RouteHandler {
    const h = this.handler;
    if (h === null) {
      throw new Error("ExecutionContext.getHandler(): no route handler");
    }
    return h;
  }
  /** The controller class of the route. */
  getClass(): ClassInfo {
    return this.getHandler().cls;
  }
  // @nestjs/core: the route this context belongs to
  setHandler(handler: RouteHandler): void {
    this.handler = handler;
  }
}
