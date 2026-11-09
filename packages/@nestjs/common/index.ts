// @nestjs/common for xisto: the decorators record what Nest keeps with reflect-metadata, from the ClassInfo the
// compiler builds for decorated classes (xisto:reflect). @nestjs/core reads it to build the application.
import { ClassInfo, MethodInfo } from "xisto:reflect";
import { Request, Response } from "express";

// ---- what the decorators record (first: decorated classes below register as soon as they are declared) ----
export const __classes: Map<string, ClassInfo> = new Map<string, ClassInfo>();
export const __controllers: Map<string, string> = new Map<string, string>();
export const __modules: Map<string, ModuleMetadata> = new Map<string, ModuleMetadata>();
export const __global: Map<string, boolean> = new Map<string, boolean>();
export const __routes: Map<string, RouteDef[]> = new Map<string, RouteDef[]>();
export const __params: Map<string, ParamDef[]> = new Map<string, ParamDef[]>();
export const __guards: Map<string, GuardDef[]> = new Map<string, GuardDef[]>();
export const __validated: Map<string, int[]> = new Map<string, int[]>();
export const __settings: Map<string, MethodSetting[]> = new Map<string, MethodSetting[]>();
export const __injects: Map<string, InjectDef[]> = new Map<string, InjectDef[]>();
export const __catches: Map<string, string[]> = new Map<string, string[]>();
export const __custom: CustomParam[] = [];
// values a constructor parameter receives without a provider class: by token (@InjectRepository) or by the
// parameter's type id (a DataSource TypeOrmModule.forRoot made). __values holds ready instances; __jsonValues the
// JSON the parameter's own type decodes into the exact instance it asks for (a Repository<User> from forFeature).
export const __values: Map<string, unknown> = new Map<string, unknown>();
export const __jsonValues: Map<string, string> = new Map<string, string>();
export const __tokens: Map<string, string> = new Map<string, string>(); // "<class id>#<parameter index>" -> token

/** HTTP status codes, as @nestjs/common names them. */
export enum HttpStatus {
  CONTINUE = 100,
  SWITCHING_PROTOCOLS = 101,
  PROCESSING = 102,
  EARLYHINTS = 103,
  OK = 200,
  CREATED = 201,
  ACCEPTED = 202,
  NON_AUTHORITATIVE_INFORMATION = 203,
  NO_CONTENT = 204,
  RESET_CONTENT = 205,
  PARTIAL_CONTENT = 206,
  AMBIGUOUS = 300,
  MOVED_PERMANENTLY = 301,
  FOUND = 302,
  SEE_OTHER = 303,
  NOT_MODIFIED = 304,
  TEMPORARY_REDIRECT = 307,
  PERMANENT_REDIRECT = 308,
  BAD_REQUEST = 400,
  UNAUTHORIZED = 401,
  PAYMENT_REQUIRED = 402,
  FORBIDDEN = 403,
  NOT_FOUND = 404,
  METHOD_NOT_ALLOWED = 405,
  NOT_ACCEPTABLE = 406,
  PROXY_AUTHENTICATION_REQUIRED = 407,
  REQUEST_TIMEOUT = 408,
  CONFLICT = 409,
  GONE = 410,
  LENGTH_REQUIRED = 411,
  PRECONDITION_FAILED = 412,
  PAYLOAD_TOO_LARGE = 413,
  URI_TOO_LONG = 414,
  UNSUPPORTED_MEDIA_TYPE = 415,
  REQUESTED_RANGE_NOT_SATISFIABLE = 416,
  EXPECTATION_FAILED = 417,
  I_AM_A_TEAPOT = 418,
  MISDIRECTED = 421,
  UNPROCESSABLE_ENTITY = 422,
  FAILED_DEPENDENCY = 424,
  PRECONDITION_REQUIRED = 428,
  TOO_MANY_REQUESTS = 429,
  INTERNAL_SERVER_ERROR = 500,
  NOT_IMPLEMENTED = 501,
  BAD_GATEWAY = 502,
  SERVICE_UNAVAILABLE = 503,
  GATEWAY_TIMEOUT = 504,
  HTTP_VERSION_NOT_SUPPORTED = 505,
}

/**
 * An error with an HTTP status: Nest answers with its body and status.
 * @example throw new HttpException({ message: "taken", statusCode: 409 }, 409)
 */
@Injectable()
export class HttpException {
  readonly response: object;
  readonly status: int;
  /** The class's name, which @Catch(SomeException) compares (set by each built-in exception). */
  exceptionName: string = "HttpException";
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
  /** The message Nest gives the error: the body's message, or the description. */
  get message(): string {
    const text = JSON.stringify(this.response);
    if (text.startsWith("\"")) {
      return JSON.parse(text) as string;
    }
    return bodyField(text, "message").length > 0 ? (JSON.parse(bodyField(text, "message")) as string) : "Http Exception";
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
@Injectable()
export class BadRequestException extends HttpException {
  constructor(message: string = "", description: string = "Bad Request") {
    super(HttpException.createBody(message, description, HttpStatus.BAD_REQUEST), HttpStatus.BAD_REQUEST);
    this.exceptionName = "BadRequestException";
  }
}
@Injectable()
export class UnauthorizedException extends HttpException {
  constructor(message: string = "", description: string = "Unauthorized") {
    super(HttpException.createBody(message, description, HttpStatus.UNAUTHORIZED), HttpStatus.UNAUTHORIZED);
    this.exceptionName = "UnauthorizedException";
  }
}
@Injectable()
export class ForbiddenException extends HttpException {
  constructor(message: string = "", description: string = "Forbidden") {
    super(HttpException.createBody(message, description, HttpStatus.FORBIDDEN), HttpStatus.FORBIDDEN);
    this.exceptionName = "ForbiddenException";
  }
}
@Injectable()
export class NotFoundException extends HttpException {
  constructor(message: string = "", description: string = "Not Found") {
    super(HttpException.createBody(message, description, HttpStatus.NOT_FOUND), HttpStatus.NOT_FOUND);
    this.exceptionName = "NotFoundException";
  }
}
@Injectable()
export class MethodNotAllowedException extends HttpException {
  constructor(message: string = "", description: string = "Method Not Allowed") {
    super(HttpException.createBody(message, description, HttpStatus.METHOD_NOT_ALLOWED), HttpStatus.METHOD_NOT_ALLOWED);
    this.exceptionName = "MethodNotAllowedException";
  }
}
@Injectable()
export class NotAcceptableException extends HttpException {
  constructor(message: string = "", description: string = "Not Acceptable") {
    super(HttpException.createBody(message, description, HttpStatus.NOT_ACCEPTABLE), HttpStatus.NOT_ACCEPTABLE);
    this.exceptionName = "NotAcceptableException";
  }
}
@Injectable()
export class RequestTimeoutException extends HttpException {
  constructor(message: string = "", description: string = "Request Timeout") {
    super(HttpException.createBody(message, description, HttpStatus.REQUEST_TIMEOUT), HttpStatus.REQUEST_TIMEOUT);
    this.exceptionName = "RequestTimeoutException";
  }
}
@Injectable()
export class ConflictException extends HttpException {
  constructor(message: string = "", description: string = "Conflict") {
    super(HttpException.createBody(message, description, HttpStatus.CONFLICT), HttpStatus.CONFLICT);
    this.exceptionName = "ConflictException";
  }
}
@Injectable()
export class GoneException extends HttpException {
  constructor(message: string = "", description: string = "Gone") {
    super(HttpException.createBody(message, description, HttpStatus.GONE), HttpStatus.GONE);
    this.exceptionName = "GoneException";
  }
}
@Injectable()
export class PreconditionFailedException extends HttpException {
  constructor(message: string = "", description: string = "Precondition Failed") {
    super(HttpException.createBody(message, description, HttpStatus.PRECONDITION_FAILED), HttpStatus.PRECONDITION_FAILED);
    this.exceptionName = "PreconditionFailedException";
  }
}
@Injectable()
export class PayloadTooLargeException extends HttpException {
  constructor(message: string = "", description: string = "Payload Too Large") {
    super(HttpException.createBody(message, description, HttpStatus.PAYLOAD_TOO_LARGE), HttpStatus.PAYLOAD_TOO_LARGE);
    this.exceptionName = "PayloadTooLargeException";
  }
}
@Injectable()
export class UnsupportedMediaTypeException extends HttpException {
  constructor(message: string = "", description: string = "Unsupported Media Type") {
    super(HttpException.createBody(message, description, HttpStatus.UNSUPPORTED_MEDIA_TYPE), HttpStatus.UNSUPPORTED_MEDIA_TYPE);
    this.exceptionName = "UnsupportedMediaTypeException";
  }
}
@Injectable()
export class UnprocessableEntityException extends HttpException {
  constructor(message: string = "", description: string = "Unprocessable Entity") {
    super(HttpException.createBody(message, description, HttpStatus.UNPROCESSABLE_ENTITY), HttpStatus.UNPROCESSABLE_ENTITY);
    this.exceptionName = "UnprocessableEntityException";
  }
}
@Injectable()
export class InternalServerErrorException extends HttpException {
  constructor(message: string = "", description: string = "Internal Server Error") {
    super(HttpException.createBody(message, description, HttpStatus.INTERNAL_SERVER_ERROR), HttpStatus.INTERNAL_SERVER_ERROR);
    this.exceptionName = "InternalServerErrorException";
  }
}
@Injectable()
export class NotImplementedException extends HttpException {
  constructor(message: string = "", description: string = "Not Implemented") {
    super(HttpException.createBody(message, description, HttpStatus.NOT_IMPLEMENTED), HttpStatus.NOT_IMPLEMENTED);
    this.exceptionName = "NotImplementedException";
  }
}
@Injectable()
export class BadGatewayException extends HttpException {
  constructor(message: string = "", description: string = "Bad Gateway") {
    super(HttpException.createBody(message, description, HttpStatus.BAD_GATEWAY), HttpStatus.BAD_GATEWAY);
    this.exceptionName = "BadGatewayException";
  }
}
@Injectable()
export class ServiceUnavailableException extends HttpException {
  constructor(message: string = "", description: string = "Service Unavailable") {
    super(HttpException.createBody(message, description, HttpStatus.SERVICE_UNAVAILABLE), HttpStatus.SERVICE_UNAVAILABLE);
    this.exceptionName = "ServiceUnavailableException";
  }
}
@Injectable()
export class GatewayTimeoutException extends HttpException {
  constructor(message: string = "", description: string = "Gateway Timeout") {
    super(HttpException.createBody(message, description, HttpStatus.GATEWAY_TIMEOUT), HttpStatus.GATEWAY_TIMEOUT);
    this.exceptionName = "GatewayTimeoutException";
  }
}

/**
 * What a guard, a filter or a custom parameter decorator sees of the current request.
 * @example ctx.switchToHttp().getRequest().headers.authorization
 */
export class ExecutionContext {
  private readonly request: Request;
  private readonly response: Response;
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
}
/** Nest's name for the context an exception filter receives. */
export type ArgumentsHost = ExecutionContext;

export interface CanActivate {}
export interface PipeTransform {}
export interface ExceptionFilter {}
export interface NestMiddleware {}
export interface OnModuleInit {}
export interface OnModuleDestroy {}
export interface OnApplicationBootstrap {}
export interface OnApplicationShutdown {}
export interface BeforeApplicationShutdown {}

/** Options of the ValidationPipe (whitelist and transform are accepted; validation always runs on class bodies). */
export class ValidationPipeOptions {
  whitelist?: boolean;
  transform?: boolean;
  forbidNonWhitelisted?: boolean;
}

/** Validates class bodies with class-validator: 400 with the messages when a rule fails. */
export class ValidationPipe {
  readonly options: ValidationPipeOptions;
  constructor(options: ValidationPipeOptions = {}) {
    this.options = options;
  }
}

/** Turns a route parameter into an integer: 400 "Validation failed (numeric string is expected)" otherwise. */
@Injectable()
export class ParseIntPipe {}
/** Turns a route parameter into a number. */
@Injectable()
export class ParseFloatPipe {}
/** Turns "true" / "false" into a boolean. */
@Injectable()
export class ParseBoolPipe {}

// ---- providers and modules ----
/** { provide: TOKEN, useValue: value } */
export class ValueProvider {
  provide: unknown;
  useValue: unknown;
}
/** { provide: TOKEN, useClass: SomeClass } */
export class ClassProvider {
  provide: unknown;
  useClass: ClassInfo;
}
/** { provide: TOKEN, useExisting: OtherToken }: an alias */
export class ExistingProvider {
  provide: unknown;
  useExisting: unknown;
}
/** { provide: TOKEN, useFactory: () => value } (factories with injected arguments are not supported yet) */
export class FactoryProvider {
  provide: unknown;
  useFactory: () => unknown;
}
/** A provider: a class, or an object naming its token. */
export type Provider = ClassInfo | ValueProvider | ClassProvider | ExistingProvider | FactoryProvider;

/** A module built by a static method (ConfigModule.forRoot(), ScheduleModule.forRoot()). */
export class DynamicModule {
  module: ClassInfo;
  imports?: unknown[];
  providers?: Provider[];
  controllers?: ClassInfo[];
  exports?: unknown[];
  global?: boolean;
}

export interface ModuleMetadata {
  imports?: unknown[];
  controllers?: ClassInfo[];
  providers?: Provider[];
  exports?: unknown[];
}

/** Nest's version type: URI puts /v<version> before the route. */
export enum VersioningType {
  URI = 0,
  HEADER = 1,
  MEDIA_TYPE = 2,
  CUSTOM = 3,
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
  source: string; // body, param, query, headers, req, res, file, custom
  key: string;
  custom: int;
  pipe: string; // "" or the pipe's class name
}
export interface GuardDef {
  method: int;
  guard: ClassInfo;
}
export interface CustomParam {
  fn: (data: string, ctx: ExecutionContext) => unknown;
}
/** A method-level setting: version, status code, filters, interceptors (method -1: the whole class). */
export interface MethodSetting {
  method: int;
  kind: string; // version, code, filter, interceptor, header
  text: string;
  code: int;
  cls: ClassInfo | null;
  value: unknown;
}
/** A constructor parameter injected by token, or optional. */
export interface InjectDef {
  index: int;
  token: unknown;
  optional: boolean;
}


function methodIndex(c: ClassInfo, m: MethodInfo): int {
  for (let i: int = 0; i < c.methods.length; i++) {
    if (c.methods[i].name === m.name) {
      return i;
    }
  }
  return -1;
}

function setting(id: string, s: MethodSetting): void {
  const list = __settings.get(id);
  if (list === null) {
    __settings.set(id, [s]);
  } else {
    list.push(s);
  }
}

/** Marks a class as a controller of routes under `prefix`. */
export function Controller(prefix: string = ""): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __classes.set(c.id, c);
    __controllers.set(c.id, prefix);
  };
}

/** Marks a class as a provider Nest can build and inject. */
export function Injectable(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __classes.set(c.id, c);
  };
}

/** Declares a module: what it imports, provides, exports and which controllers it has. */
export function Module(meta: ModuleMetadata): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __classes.set(c.id, c);
    __modules.set(c.id, meta);
  };
}

/** Makes a module's exported providers visible to every module. */
export function Global(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __global.set(c.id, true);
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
export function Patch(path: string = ""): (c: ClassInfo, m: MethodInfo) => void {
  return route("PATCH", path);
}
export function Delete(path: string = ""): (c: ClassInfo, m: MethodInfo) => void {
  return route("DELETE", path);
}

/**
 * The route's version: with URI versioning, @Version("1") serves the route at /v1/...
 * Works on a method or on the whole controller.
 */
export function Version(version: string): (c: ClassInfo, m: MethodInfo | null) => void {
  return (c: ClassInfo, m: MethodInfo | null): void => {
    setting(c.id, { method: m === null ? -1 : methodIndex(c, m), kind: "version", text: version, code: 0, cls: null, value: null });
  };
}

/** The status code a route answers with when it succeeds (by default 201 for POST, 200 for the rest). */
export function HttpCode(status: int): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {
    setting(c.id, { method: methodIndex(c, m), kind: "code", text: "", code: status, cls: null, value: null });
  };
}

/** A response header the route always sets. */
export function Header(name: string, value: string): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {
    setting(c.id, { method: methodIndex(c, m), kind: "header", text: name, code: 0, cls: null, value: value });
  };
}

/** Exception filters for a route, or for every route of the controller. */
export function UseFilters(filter: ClassInfo): (c: ClassInfo, m: MethodInfo | null) => void {
  return (c: ClassInfo, m: MethodInfo | null): void => {
    setting(c.id, { method: m === null ? -1 : methodIndex(c, m), kind: "filter", text: "", code: 0, cls: filter, value: null });
  };
}

/** Interceptors for a route (FileInterceptor("file") reads a multipart upload). */
export function UseInterceptors(interceptor: unknown): (c: ClassInfo, m: MethodInfo | null) => void {
  return (c: ClassInfo, m: MethodInfo | null): void => {
    setting(c.id, { method: m === null ? -1 : methodIndex(c, m), kind: "interceptor", text: "", code: 0, cls: null, value: interceptor });
  };
}

/** The exceptions an exception filter handles (none: every exception). */
export function Catch(exception: ClassInfo | null = null): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __classes.set(c.id, c);
    const list: string[] = [];
    if (exception !== null) {
      list.push(exception.name);
    }
    __catches.set(c.id, list);
  };
}

function param(source: string, key: string, custom: int, pipe: ClassInfo | null): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return (c: ClassInfo, m: MethodInfo | null, index: int): void => {
    if (m === null) {
      return;
    }
    const d: ParamDef = { method: methodIndex(c, m), index: index, source: source, key: key, custom: custom, pipe: pipe === null ? "" : pipe.name };
    const list = __params.get(c.id);
    if (list === null) {
      __params.set(c.id, [d]);
    } else {
      list.push(d);
    }
  };
}
/** The request body, or one of its fields. */
export function Body(key: string = "", pipe: ClassInfo | null = null): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("body", key, -1, pipe);
}
/** A route parameter (:id); with ParseIntPipe, as an integer. */
export function Param(key: string = "", pipe: ClassInfo | null = null): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("param", key, -1, pipe);
}
/** A query string value; with ParseIntPipe, as an integer. */
export function Query(key: string = "", pipe: ClassInfo | null = null): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("query", key, -1, pipe);
}
/** A request header. */
export function Headers(key: string = ""): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("headers", key, -1, null);
}
/** The Express request. */
export function Req(): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("req", "", -1, null);
}
/** The Express response: the route writes the answer itself (status, headers, send). */
export function Res(): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("res", "", -1, null);
}
/** The file of a multipart upload read by FileInterceptor. */
export function UploadedFile(): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return param("file", "", -1, null);
}

// const UserId = createParamDecorator((data, ctx) => ...); then @UserId() on a parameter
export function createParamDecorator(fn: (data: string, ctx: ExecutionContext) => unknown): () => (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  const id = __custom.length;
  __custom.push({ fn: fn });
  return (): (c: ClassInfo, m: MethodInfo | null, index: int) => void => param("custom", "", id, null);
}

function injectDef(c: ClassInfo, index: int, token: unknown, optional: boolean): void {
  const list = __injects.get(c.id);
  const d: InjectDef = { index: index, token: token, optional: optional };
  if (list === null) {
    __injects.set(c.id, [d]);
    return;
  }
  for (const x of list) {
    if (x.index === index) {
      if (token !== null) {
        x.token = token;
      }
      if (optional) {
        x.optional = true;
      }
      return;
    }
  }
  list.push(d);
}

/** Injects the provider registered under a token (a string, or a class) into a constructor parameter. */
export function Inject(token: unknown): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return (c: ClassInfo, m: MethodInfo | null, index: int): void => {
    if (m === null) {
      injectDef(c, index, token, false);
    }
  };
}

/** The parameter gets null when no provider has its token. */
export function Optional(): (c: ClassInfo, m: MethodInfo | null, index: int) => void {
  return (c: ClassInfo, m: MethodInfo | null, index: int): void => {
    if (m === null) {
      injectDef(c, index, null, true);
    }
  };
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

// ---- Logger ----
/** The log levels Nest's logger prints. */
export type LogLevel = "log" | "error" | "warn" | "debug" | "verbose" | "fatal";

/** Where Logger writes: app.useLogger(other) replaces Nest's console output (nestjs-pino does). */
export interface LoggerService {
  log: (message: string, context: string) => void;
  error: (message: string, context: string) => void;
  warn: (message: string, context: string) => void;
  debug: (message: string, context: string) => void;
  verbose: (message: string, context: string) => void;
}

const logState: LoggerState = { target: null, timestamp: 0 };
interface LoggerState {
  target: LoggerService | null;
  timestamp: number;
}

function pad2(n: int): string {
  return n < 10 ? `0${n}` : `${n}`;
}

// Nest's console line: "[Nest] 1234  - 11/10/2026, 3:04:05 PM     LOG [Context] message" (no colors: NO_COLOR)
function consoleLine(level: string, message: string, context: string): void {
  const d = new Date();
  let h = d.getHours();
  const ampm = h >= 12 ? "PM" : "AM";
  h = h % 12;
  if (h === 0) {
    h = 12;
  }
  const stamp = `${pad2(d.getMonth() + 1)}/${pad2(d.getDate())}/${d.getFullYear()}, ${h}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())} ${ampm}`;
  const label = level.toUpperCase().padStart(7, " ");
  const ctx = context.length > 0 ? `[${context}] ` : "";
  const line = `[Nest] ${process.pid}  - ${stamp} ${label} ${ctx}${message}`;
  if (level === "error" || level === "fatal") {
    console.error(line);
  } else {
    console.log(line);
  }
}

function emit(level: string, message: string, context: string): void {
  const t = logState.target;
  if (t !== null) {
    if (level === "error" || level === "fatal") {
      t.error(message, context);
    } else if (level === "warn") {
      t.warn(message, context);
    } else if (level === "debug") {
      t.debug(message, context);
    } else if (level === "verbose") {
      t.verbose(message, context);
    } else {
      t.log(message, context);
    }
    return;
  }
  consoleLine(level, message, context);
}

/**
 * Nest's logger: new Logger(MyService.name).log("started").
 * Compatibility: messages are strings; the line is Nest's ConsoleLogger format without colors.
 */
export class Logger {
  private readonly context: string;
  constructor(context: string = "") {
    this.context = context;
  }
  log(message: string, context: string = ""): void {
    emit("log", message, context.length > 0 ? context : this.context);
  }
  error(message: string, trace: string = "", context: string = ""): void {
    emit("error", message, context.length > 0 ? context : this.context);
    if (trace.length > 0 && logState.target === null) {
      console.error(trace);
    }
  }
  warn(message: string, context: string = ""): void {
    emit("warn", message, context.length > 0 ? context : this.context);
  }
  debug(message: string, context: string = ""): void {
    emit("debug", message, context.length > 0 ? context : this.context);
  }
  verbose(message: string, context: string = ""): void {
    emit("verbose", message, context.length > 0 ? context : this.context);
  }
  fatal(message: string, context: string = ""): void {
    emit("fatal", message, context.length > 0 ? context : this.context);
  }
  static log(message: string, context: string = ""): void {
    emit("log", message, context);
  }
  static error(message: string, trace: string = "", context: string = ""): void {
    emit("error", message, context);
  }
  static warn(message: string, context: string = ""): void {
    emit("warn", message, context);
  }
  static debug(message: string, context: string = ""): void {
    emit("debug", message, context);
  }
  /** Sends every Logger line to another logger (app.useLogger does this). */
  static overrideLogger(target: LoggerService): void {
    logState.target = target;
  }
}

// the JSON of one field of a JSON body ("" when absent)
export declare function bodyField(body: string, key: string): string;
