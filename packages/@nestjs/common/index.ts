// @nestjs/common for xisto: the decorators record what Nest keeps with reflect-metadata, from the ClassInfo the
// compiler builds for decorated classes (xisto:reflect). @nestjs/core reads it to build the application.
import { ClassInfo, MethodInfo } from "xisto:reflect";
import { Request } from "express";

export enum HttpStatus {
  CONTINUE = 100,
  OK = 200,
  CREATED = 201,
  ACCEPTED = 202,
  NO_CONTENT = 204,
  BAD_REQUEST = 400,
  UNAUTHORIZED = 401,
  FORBIDDEN = 403,
  NOT_FOUND = 404,
  METHOD_NOT_ALLOWED = 405,
  CONFLICT = 409,
  UNPROCESSABLE_ENTITY = 422,
  INTERNAL_SERVER_ERROR = 500,
}

// throw new HttpException("message" | { ...any body }, status): the body Nest sends
export class HttpException {
  readonly response: object;
  readonly status: int;
  constructor(response: object, status: int) {
    this.response = response;
    this.status = status;
  }
  getStatus(): int {
    return this.status;
  }
  getResponse(): object {
    return this.response;
  }

  // Nest's body for the built-in exceptions: { message, statusCode }, or { message, error, statusCode } with a message
  static createBody(message: string, description: string, statusCode: int): object {
    if (!message) {
      const plain: ErrorBody = { message: description, statusCode: statusCode };
      return plain;
    }
    const full: ErrorBody = { message: message, error: description, statusCode: statusCode };
    return full;
  }
}

interface ErrorBody {
  message: string;
  error?: string;
  statusCode: int;
}

// the built-in HTTP exceptions, as in @nestjs/common: new NotFoundException(), new NotFoundException("User not found")
export class BadRequestException extends HttpException {
  constructor(message: string = "", description: string = "Bad Request") {
    super(HttpException.createBody(message, description, HttpStatus.BAD_REQUEST), HttpStatus.BAD_REQUEST);
  }
}
export class UnauthorizedException extends HttpException {
  constructor(message: string = "", description: string = "Unauthorized") {
    super(HttpException.createBody(message, description, HttpStatus.UNAUTHORIZED), HttpStatus.UNAUTHORIZED);
  }
}
export class ForbiddenException extends HttpException {
  constructor(message: string = "", description: string = "Forbidden") {
    super(HttpException.createBody(message, description, HttpStatus.FORBIDDEN), HttpStatus.FORBIDDEN);
  }
}
export class NotFoundException extends HttpException {
  constructor(message: string = "", description: string = "Not Found") {
    super(HttpException.createBody(message, description, HttpStatus.NOT_FOUND), HttpStatus.NOT_FOUND);
  }
}
export class ConflictException extends HttpException {
  constructor(message: string = "", description: string = "Conflict") {
    super(HttpException.createBody(message, description, HttpStatus.CONFLICT), HttpStatus.CONFLICT);
  }
}
export class UnprocessableEntityException extends HttpException {
  constructor(message: string = "", description: string = "Unprocessable Entity") {
    super(HttpException.createBody(message, description, HttpStatus.UNPROCESSABLE_ENTITY), HttpStatus.UNPROCESSABLE_ENTITY);
  }
}
export class InternalServerErrorException extends HttpException {
  constructor(message: string = "", description: string = "Internal Server Error") {
    super(HttpException.createBody(message, description, HttpStatus.INTERNAL_SERVER_ERROR), HttpStatus.INTERNAL_SERVER_ERROR);
  }
}

export class ExecutionContext {
  private readonly request: Request;
  constructor(request: Request) {
    this.request = request;
  }
  switchToHttp(): ExecutionContext {
    return this;
  }
  getRequest(): Request {
    return this.request;
  }
}

export interface CanActivate {}
export interface PipeTransform {}
export class ValidationPipe {}

export interface ModuleMetadata {
  imports?: ClassInfo[];
  controllers?: ClassInfo[];
  providers?: ClassInfo[];
  exports?: ClassInfo[];
}

// ---- what the decorators record, read by @nestjs/core ----
export interface RouteDef {
  verb: string;
  path: string;
  method: int;
}
export interface ParamDef {
  method: int;
  index: int;
  source: string; // body, param, query, headers, custom
  key: string;
  custom: int;
}
export interface GuardDef {
  method: int;
  guard: ClassInfo;
}
export interface CustomParam {
  fn: (data: string, ctx: ExecutionContext) => unknown;
}

export const __classes: Map<string, ClassInfo> = new Map<string, ClassInfo>();
export const __controllers: Map<string, string> = new Map<string, string>();
export const __modules: Map<string, ModuleMetadata> = new Map<string, ModuleMetadata>();
export const __routes: Map<string, RouteDef[]> = new Map<string, RouteDef[]>();
export const __params: Map<string, ParamDef[]> = new Map<string, ParamDef[]>();
export const __guards: Map<string, GuardDef[]> = new Map<string, GuardDef[]>();
export const __validated: Map<string, int[]> = new Map<string, int[]>();
export const __custom: CustomParam[] = [];
// values a constructor parameter receives without a provider class: by token (@Inject(token), @InjectRepository) or by
// the parameter's type id (a DataSource TypeOrmModule.forRoot made). __values holds ready instances; __jsonValues the
// JSON the parameter's own type decodes into the exact instance it asks for (a Repository<User> from forFeature).
export const __values: Map<string, unknown> = new Map<string, unknown>();
export const __jsonValues: Map<string, string> = new Map<string, string>();
export const __tokens: Map<string, string> = new Map<string, string>(); // "<class id>#<parameter index>" -> token

/** @Inject(token) on a constructor parameter: it receives the value registered under that token */
export function Inject(token: string): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return (c: ClassInfo, m: MethodInfo | null, index: int): void => {
    if (m === null) {
      __tokens.set(c.id + "#" + String(index), token);
    }
  };
}

function methodIndex(c: ClassInfo, m: MethodInfo): int {
  for (let i: int = 0; i < c.methods.length; i++) {
    if (c.methods[i].name === m.name) {
      return i;
    }
  }
  return -1;
}

export function Controller(prefix: string = ""): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __classes.set(c.id, c);
    __controllers.set(c.id, prefix);
  };
}

export function Injectable(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __classes.set(c.id, c);
  };
}

export function Module(meta: ModuleMetadata): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __classes.set(c.id, c);
    __modules.set(c.id, meta);
  };
}

function route(verb: string, path: string): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {
    const list = __routes.get(c.id);
    const r: RouteDef = { verb: verb, path: path, method: methodIndex(c, m) };
    if (list === null) {
      __routes.set(c.id, [r]);
    } else {
      list.push(r);
    }
  };
}
export function Get(path: string = ""): (c: ClassInfo, m: MethodInfo) => void {
  return route("GET", path);
}
export function Post(path: string = ""): (c: ClassInfo, m: MethodInfo) => void {
  return route("POST", path);
}
export function Put(path: string = ""): (c: ClassInfo, m: MethodInfo) => void {
  return route("PUT", path);
}
export function Delete(path: string = ""): (c: ClassInfo, m: MethodInfo) => void {
  return route("DELETE", path);
}

function param(source: string, key: string, custom: int): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return (c: ClassInfo, m: MethodInfo | null, index: int): void => {
    if (m === null) {
      return;
    }
    const d: ParamDef = { method: methodIndex(c, m), index: index, source: source, key: key, custom: custom };
    const list = __params.get(c.id);
    if (list === null) {
      __params.set(c.id, [d]);
    } else {
      list.push(d);
    }
  };
}
export function Body(key: string = ""): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("body", key, -1);
}
export function Param(key: string = ""): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("param", key, -1);
}
export function Query(key: string = ""): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("query", key, -1);
}
export function Headers(key: string = ""): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("headers", key, -1);
}

// const UserId = createParamDecorator((data, ctx) => ...); then @UserId() on a parameter
export function createParamDecorator(fn: (data: string, ctx: ExecutionContext) => unknown): () => (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  const id = __custom.length;
  __custom.push({ fn: fn });
  return (): (c: ClassInfo, m: MethodInfo | null, index: int) => void => param("custom", "", id);
}

export function UseGuards(guard: ClassInfo): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {
    const d: GuardDef = { method: methodIndex(c, m), guard: guard };
    const list = __guards.get(c.id);
    if (list === null) {
      __guards.set(c.id, [d]);
    } else {
      list.push(d);
    }
  };
}

export function UsePipes(pipe: ValidationPipe): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {
    const list = __validated.get(c.id);
    const i = methodIndex(c, m);
    if (list === null) {
      __validated.set(c.id, [i]);
    } else {
      list.push(i);
    }
  };
}

// the JSON of one field of a JSON body ("" when absent)
export declare function bodyField(body: string, key: string): string;
