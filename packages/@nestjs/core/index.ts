// @nestjs/core for xisto: NestFactory.create(AppModule) walks the modules @Module recorded (static and dynamic),
// builds every provider and controller once (constructor parameters by type or by @Inject token, like Nest's
// injector), and registers each route on xisto's HTTP server with versioning, guards, interceptors, pipes,
// exception filters, parameter decorators, the ValidationPipe and Nest's error bodies. A request runs as in Nest:
// middleware, guards (global, controller, route), interceptors around the route (same order), then the pipes
// and the handler; an exception anywhere goes to the exception filters.
import { ClassInfo, MethodInfo } from "xisto:reflect";
import { Observable } from "rxjs";
import { Request, RequestHandler, Response } from "express";
import { validationMessages } from "class-validator";
import {
  __catches,
  __classes,
  __controllers,
  VERSION_NEUTRAL,
  __custom,
  __global,
  __guards,
  __injects,
  __modules,
  __params,
  __routes,
  __settings,
  __validated,
  __jsonValues,
  __tokens,
  __values,
  __appFactories,
  bodyField,
  CallHandler,
  ClassProvider,
  CorsOptions,
  DynamicModule,
  ExecutionContext,
  ExistingProvider,
  FactoryProvider,
  GuardDef,
  HttpException,
  InjectDef,
  Logger,
  LoggerService,
  MethodSetting,
  ModuleMetadata,
  ParamDef,
  Provider,
  Reflector,
  RouteHandler,
  RouteDef,
  ValidationPipe,
  ValueProvider,
  VersioningType,
} from "@nestjs/common";
import { FileInterceptorSpec, readUpload, UploadedFileData, uploadField } from "./upload";

export { FileInterceptorSpec, UploadedFileData } from "./upload";
export { Reflector } from "@nestjs/common"; // (Nest exports it from @nestjs/core)
export { CorsOptions } from "@nestjs/common";

/** Options of NestFactory.create. */
/** app.enableShutdownHooks(signals, { useProcessExit }) */
export interface ShutdownHooksOptions {
  useProcessExit?: boolean;
}
export class NestApplicationOptions {
  cors?: boolean;
  bufferLogs?: boolean;
  logger?: boolean;
}

/** app.enableVersioning({ type: VersioningType.URI, defaultVersion: "1" }) */
export class VersioningOptions {
  type: VersioningType;
  defaultVersion?: string;
  prefix?: string;
}

/** The token of a global interceptor, filter, guard or pipe given as a provider. */
export const APP_INTERCEPTOR = "APP_INTERCEPTOR";
export const APP_FILTER = "APP_FILTER";
export const APP_GUARD = "APP_GUARD";
export const APP_PIPE = "APP_PIPE";

interface Route {
  verb: string;
  path: string;
  instance: unknown;
  method: MethodInfo;
  handler: RouteHandler;
  interceptors: unknown[];
  interceptorMethods: MethodInfo[];
  params: ParamDef[];
  guards: unknown[];
  guardMethods: MethodInfo[];
  validate: boolean;
  status: int;
  cors: boolean;
  headers: string[];
  filters: unknown[];
  filterInfos: ClassInfo[];
  upload: string; // the multipart field FileInterceptor reads ("" none)
  middleware: RequestHandler[];
}

const JSON_TYPE = "application/json; charset=utf-8";

// a GET route outside the controllers (SwaggerModule.setup serves the API documentation through one)
interface ExtraRoute {
  path: string;
  handler: (req: HttpRequest) => HttpResponse;
}

/** What DiscoveryService gives for one provider or controller. */
/** The module a provider was declared in (InstanceWrapper.host, as Nest's Module: its metatype is the module class). */
export class ModuleHost {
  metatype: ClassInfo | null = null;
}

export class InstanceWrapper {
  name: string = "";
  token: unknown = null;
  instance: unknown = null;
  metatype: ClassInfo | null = null;
  host: ModuleHost | null = null; // (null for controllers and the application's own globals)
}

/** The providers and controllers the application built (import DiscoveryModule to inject it). */
@Injectable0()
export class DiscoveryService {
  getProviders(): InstanceWrapper[] {
    return container.wrappers.filter((w: InstanceWrapper): boolean => !isController(w));
  }
  getControllers(): InstanceWrapper[] {
    return container.wrappers.filter((w: InstanceWrapper): boolean => isController(w));
  }
}

function isController(w: InstanceWrapper): boolean {
  const m = w.metatype;
  return m !== null && __controllers.has(m.id);
}

// (DiscoveryService is decorated here without importing Injectable twice under the same name)
function Injectable0(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __classes.set(c.id, c);
  };
}

/** Makes DiscoveryService injectable. */
@Module({ providers: [DiscoveryService], exports: [DiscoveryService] })
export class DiscoveryModule {}

function Module(meta: ModuleMetadata): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    __classes.set(c.id, c);
    __modules.set(c.id, meta);
  };
}

// ---- the container: tokens, providers, instances ----
interface Entry {
  key: string;
  token: unknown;
  provider: unknown; // a ClassInfo, or one of the provider objects
  host: ModuleHost | null; // the module that declared it
}

let declaringModule: ModuleHost | null = null; // the module whose providers register() is reading

class Container {
  entries: Map<string, Entry> = new Map<string, Entry>();
  instances: Map<string, unknown> = new Map<string, unknown>();
  built: string[] = [];
  wrappers: InstanceWrapper[] = [];
  controllers: ClassInfo[] = [];
  globals: unknown[] = []; // APP_FILTER / APP_GUARD / APP_INTERCEPTOR providers
  collected: boolean = false;
}

// the modules, providers and controllers the root reaches, read once
function ensureCollected(root: ClassInfo): void {
  if (container.collected) {
    return;
  }
  container.collected = true;
  addEntry(Reflector, Reflector); // (Nest provides it to every module)
  collect(root, new Map<string, boolean>());
}

const container: Container = new Container();

/** The key a token is stored under: a class by its id, a string as itself. */
export function tokenKey(token: unknown): string {
  if (typeof token === "string") {
    return "s:" + (token as string);
  }
  if (typeof token === "symbol") { /* Symbol("X"): itself (two symbols with one description are two tokens) */
    return "y:" + String((token as symbol).__id);
  }
  const c = token as ClassInfo;
  return "c:" + c.id;
}

function tokenName(token: unknown): string {
  if (typeof token === "string") {
    return token as string;
  }
  if (typeof token === "symbol") {
    return String(token as symbol);
  }
  return (token as ClassInfo).name;
}

function asClass(x: unknown): ClassInfo | null {
  if (x instanceof ValueProvider || x instanceof ClassProvider || x instanceof ExistingProvider || x instanceof FactoryProvider) {
    return null;
  }
  if (x instanceof DynamicModule || typeof x === "string" || typeof x === "symbol") {
    return null;
  }
  return x as ClassInfo;
}

function register(p: unknown): void {
  if (p instanceof ValueProvider) {
    const v = p as ValueProvider;
    addEntry(v.provide, p);
    return;
  }
  if (p instanceof ClassProvider) {
    const v = p as ClassProvider;
    addEntry(v.provide, p);
    return;
  }
  if (p instanceof ExistingProvider) {
    const v = p as ExistingProvider;
    addEntry(v.provide, p);
    return;
  }
  if (p instanceof FactoryProvider) {
    const v = p as FactoryProvider;
    addEntry(v.provide, p);
    return;
  }
  const c = asClass(p);
  if (c !== null) {
    addEntry(c, c);
  }
}

function addEntry(token: unknown, provider: unknown): void {
  const key = tokenKey(token);
  if (key === "s:APP_FILTER" || key === "s:APP_GUARD" || key === "s:APP_INTERCEPTOR" || key === "s:APP_PIPE") {
    container.globals.push(provider);
    return;
  }
  container.entries.set(key, { key: key, token: token, provider: provider, host: declaringModule });
}

// the modules the root reaches through its imports (static and dynamic), in Nest's order
function collect(m: unknown, seen: Map<string, boolean>): void {
  if (m instanceof DynamicModule) {
    const d = m as DynamicModule;
    const key = "dyn:" + d.module.id;
    collectMeta(d.module, seen);
    if (seen.has(key)) {
      return;
    }
    seen.set(key, true);
    for (const i of d.imports || []) {
      collect(i, seen);
    }
    const hd = new ModuleHost();
    hd.metatype = d.module;
    for (const p of d.providers || []) {
      declaringModule = hd;
      register(p);
    }
    declaringModule = null;
    for (const c of d.controllers || []) {
      container.controllers.push(c);
    }
    return;
  }
  const c = asClass(m);
  if (c !== null) {
    collectMeta(c, seen);
  }
}

function collectMeta(c: ClassInfo, seen: Map<string, boolean>): void {
  if (seen.has(c.id)) {
    return;
  }
  seen.set(c.id, true);
  const meta = __modules.get(c.id);
  if (meta === null) {
    throw new Error(`Nest can't find the module ${c.name}: is it decorated with @Module()?`);
  }
  for (const i of meta.imports || []) {
    collect(i, seen);
  }
  const hm = new ModuleHost();
  hm.metatype = c;
  for (const p of meta.providers || []) {
    declaringModule = hm;
    register(p);
  }
  declaringModule = null;
  for (const ctl of meta.controllers || []) {
    container.controllers.push(ctl);
  }
}

// (every provider is visible to every module: a module's exports narrow nothing here, as in a Nest app that compiles)
function resolveToken(token: unknown, optional: boolean, who: string, index: int, depth: int): unknown {
  const key = tokenKey(token);
  const have = container.instances.get(key);
  if (have !== null) {
    return have;
  }
  const e = container.entries.get(key);
  if (e === null) {
    // a value a package registered without a provider (TypeOrmModule's DataSource), asked by inject: [DataSource]
    // or by its token string, as construct() finds it for a constructor parameter
    const v = __values.get(typeof token === "string" ? (token as string) : typeof token === "symbol" ? "" : (token as ClassInfo).id);
    if (v !== null) {
      return v;
    }
    if (optional) {
      return null;
    }
    throw new Error(`Nest can't resolve dependencies of the ${who} (?). Please make sure that the argument ${tokenName(token)} at index [${index}] is available in the current module context.`);
  }
  const made = build(e.provider, depth + 1);
  container.instances.set(key, made);
  addWrapper(e, made);
  return made;
}

function addWrapper(e: Entry, instance: unknown): void {
  const cls = asClass(e.provider);
  container.wrappers.push({ name: tokenName(e.token), token: e.token, instance: instance, metatype: cls, host: e.host });
}

function build(provider: unknown, depth: int): unknown {
  if (depth > 64) {
    throw new Error("Nest can't resolve dependencies: a circular dependency between providers");
  }
  if (provider instanceof ValueProvider) {
    return (provider as ValueProvider).useValue;
  }
  if (provider instanceof FactoryProvider) {
    const fp = provider as FactoryProvider;
    const args: unknown[] = [];
    const inject = fp.inject;
    if (inject !== null) {
      let i = 0;
      for (const t of inject) {
        args.push(resolveToken(t, false, tokenName(fp.provide), i, depth + 1));
        i++;
      }
    }
    return fp.useFactory(...args);
  }
  if (provider instanceof ExistingProvider) {
    return resolveToken((provider as ExistingProvider).useExisting, false, "alias", 0, depth);
  }
  if (provider instanceof ClassProvider) {
    return construct((provider as ClassProvider).useClass, depth);
  }
  const c = provider as ClassInfo;
  return construct(c, depth);
}

/** Builds a class with its constructor parameters resolved (by @Inject token, or by type). */
function construct(ci: ClassInfo, depth: int): unknown {
  const injects0 = __injects.get(ci.id);
  const injects: InjectDef[] = injects0 || [];
  const args: unknown[] = [];
  for (const p of ci.params) {
    let token: unknown = null;
    let optional = false;
    for (const d of injects) {
      if (d.index === p.index) {
        token = d.token;
        optional = d.optional;
      }
    }
    // a value registered without a provider class (TypeOrmModule): by token (@InjectRepository) or by the
    // parameter's type id (a DataSource). __values holds ready instances; __jsonValues the JSON the parameter's own
    // type decodes into the exact instance it asks for (a Repository<User>).
    const key = __tokens.get(ci.id + "#" + String(p.index)) ?? (typeof token === "string" ? (token as string) : p.type.id);
    const value = __values.get(key);
    if (value !== null) {
      args.push(value);
      continue;
    }
    const json = __jsonValues.get(key);
    if (json !== null) {
      args.push(p.type.decode(json));
      continue;
    }
    if (token === null) {
      const byType = __classes.get(p.type.id);
      if (byType === null) {
        if (optional) {
          args.push(null);
          continue;
        }
        throw new Error(`Nest can't resolve dependencies of the ${ci.name} (${p.type.id} at index [${p.index}]). Make sure it is available in the current module.`);
      }
      token = byType;
    }
    args.push(resolveToken(token, optional, ci.name, p.index, depth));
  }
  return ci.construct(args);
}

/** The instance under a token (app.get(Service)). */
export function getInstance(token: unknown): unknown {
  return resolveToken(token, false, "application", 0, 0);
}

// "/" + the non-empty parts without their slashes: the path Nest gives prefix + version + controller + route
export function joinPath(parts: string[]): string {
  let out = "";
  for (const p of parts) {
    let s = p;
    while (s.startsWith("/")) {
      s = s.slice(1, s.length);
    }
    while (s.length > 0 && s.endsWith("/")) {
      s = s.slice(0, s.length - 1);
    }
    if (s.length > 0) {
      out = out + "/" + s;
    }
  }
  if (out.length === 0) {
    return "/";
  }
  return out;
}

// the controllers the root module reaches (@nestjs/swagger documents them)
export function moduleControllers(root: ClassInfo): ClassInfo[] {
  ensureCollected(root);
  return container.controllers;
}

function settingsOf(c: ClassInfo, method: int, kind: string): MethodSetting[] {
  const all0 = __settings.get(c.id);
  const all: MethodSetting[] = all0 || [];
  return all.filter((s: MethodSetting): boolean => s.kind === kind && (s.method === method || s.method === -1));
}

// (headers a middleware set, such as helmet's, go on every reply)
function reply(r: Route, response: Response, status: int, contentType: string, body: string): HttpResponse {
  const ct = body.length === 0 ? "" : contentType;
  response.statusCode = status;
  response.headersReady(ct);
  let res = HttpResponse.of(status, ct, body);
  for (let i = 0; i < response.headerCount(); i++) {
    if (response.headerName(i).toLowerCase() !== "content-type") {
      res = res.header(response.headerName(i), response.headerValue(i));
    }
  }
  for (let i = 0; i + 1 < r.headers.length; i += 2) {
    res = res.header(r.headers[i], r.headers[i + 1]);
  }
  if (r.cors) {
    return res.header("Access-Control-Allow-Origin", "*");
  }
  return res;
}

function fromResponse(r: Route, res: Response): HttpResponse {
  const body = res.getBody();
  res.headersReady(body.length === 0 ? "" : "text/html; charset=utf-8");
  let ct = res.getHeader("Content-Type");
  let out = HttpResponse.of(res.statusCode, body.length === 0 && ct === null ? "" : ct === null ? "text/html; charset=utf-8" : ct, body);
  for (let i = 0; i < res.headerCount(); i++) {
    if (res.headerName(i).toLowerCase() !== "content-type") {
      out = out.header(res.headerName(i), res.headerValue(i));
    }
  }
  for (let i = 0; i + 1 < r.headers.length; i += 2) {
    out = out.header(r.headers[i], r.headers[i + 1]);
  }
  if (r.cors) {
    return out.header("Access-Control-Allow-Origin", "*");
  }
  return out;
}

function badRequest(r: Route, response: Response, messages: string[]): HttpResponse {
  return reply(r, response, 400, JSON_TYPE, `{"message":${JSON.stringify(messages)},"error":"Bad Request","statusCode":400}`);
}

function badRequestText(r: Route, response: Response, message: string): HttpResponse {
  return reply(r, response, 400, JSON_TYPE, `{"message":${JSON.stringify(message)},"error":"Bad Request","statusCode":400}`);
}

// the exception filter that catches this error, or -1
function filterFor(r: Route, e: Error): int {
  const cause = e.__thrown;
  for (let i = 0; i < r.filters.length; i++) {
    const names0 = __catches.get(r.filterInfos[i].id);
    const names: string[] = names0 || [];
    if (names.length === 0) {
      return i;
    }
    if (cause instanceof HttpException) {
      for (const n of names) {
        if (n === "HttpException" || n === (cause as HttpException).exceptionName) {
          return i;
        }
      }
    }
  }
  return -1;
}

function failure(r: Route, e: Error, ctx: ExecutionContext): HttpResponse {
  const f = filterFor(r, e);
  if (f >= 0) {
    const m = r.filterInfos[f].methods.find((x: MethodInfo): boolean => x.name === "catch");
    if (m !== null) {
      const cause = e.__thrown;
      let thrown: unknown = e;
      if (cause !== null) {
        thrown = cause;
      }
      m.invoke(r.filters[f], [thrown, ctx]);
      return fromResponse(r, ctx.getResponse());
    }
  }
  const cause = e.__thrown;
  if (cause instanceof HttpException) {
    const h = cause as HttpException;
    const text = JSON.stringify(h.response);
    if (text.startsWith("\"")) {
      return reply(r, ctx.getResponse(), h.status, JSON_TYPE, `{"statusCode":${h.status},"message":${text}}`);
    }
    return reply(r, ctx.getResponse(), h.status, JSON_TYPE, text);
  }
  new Logger("ExceptionsHandler").error(e.message, e.stack);
  return reply(r, ctx.getResponse(), 500, JSON_TYPE, `{"statusCode":500,"message":"Internal server error"}`);
}

function isInt(s: string): boolean {
  if (s.length === 0) {
    return false;
  }
  let i = 0;
  if (s.startsWith("-")) {
    i = 1;
  }
  if (i >= s.length) {
    return false;
  }
  for (; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c < 48 || c > 57) {
      return false;
    }
  }
  return true;
}

function handle(r: Route, req: HttpRequest): HttpResponse {
  const request = new Request();
  request.method = req.method;
  request.url = req.path;
  request.path = req.path;
  request.body = req.body;
  for (const h of ["authorization", "content-type", "accept", "user-agent", "accept-encoding", "x-request-id"]) {
    const v = req.header(h);
    if (v.length > 0) {
      request.setHeaderValue(h, v);
    }
  }
  const response = new Response();
  const ctx = new ExecutionContext(request, response);
  ctx.setHandler(r.handler);
  try {
    for (const mw of r.middleware) {
      let passed = false;
      mw(request, response, (): void => {
        passed = true;
      });
      if (!passed || response.finished) {
        return fromResponse(r, response);
      }
    }
    for (let g: int = 0; g < r.guards.length; g++) {
      if (!settleBoolean(r.guardMethods[g].invoke(r.guards[g], [ctx]))) {
        return reply(r, response, 403, JSON_TYPE, `{"message":"Forbidden resource","error":"Forbidden","statusCode":403}`);
      }
    }
    const run = new RouteRun();
    // the pipes and the handler: what the innermost next.handle() runs
    let chain = (): unknown => runHandler(r, req, request, response, ctx, run);
    for (let i = r.interceptors.length - 1; i >= 0; i--) {
      const next = chain;
      const instance = r.interceptors[i];
      const intercept = r.interceptorMethods[i];
      chain = (): unknown => observed(intercept.invoke(instance, [ctx, new CallHandler(next)]));
    }
    // subscribed as Nest does: finalize() callbacks run once the outermost interceptor has the value
    let result: unknown = null;
    new Observable<unknown>(chain).subscribe((v: unknown): void => {
      result = v;
    });
    const answered = run.early;
    if (answered !== null) {
      return answered;
    }
    if (run.usesRes) {
      return fromResponse(r, response);
    }
    if (result !== run.result) {
      // an interceptor gave another value: sent as Nest's Express adapter sends it (an object or an array as
      // JSON, anything else as text)
      if (result === null) {
        return reply(r, response, r.status, "text/html; charset=utf-8", "");
      }
      const kind = typeof result;
      if (kind === "string") {
        return reply(r, response, r.status, "text/html; charset=utf-8", result as string);
      }
      if (kind === "number" || kind === "boolean") {
        return reply(r, response, r.status, "text/html; charset=utf-8", JSON.stringify(result));
      }
      return reply(r, response, r.status, JSON_TYPE, JSON.stringify(result));
    }
    if (r.method.returns.kind === "void" || result === null) {
      return reply(r, response, r.status, "text/html; charset=utf-8", "");
    }
    if (r.method.returns.kind === "string") {
      return reply(r, response, r.status, "text/html; charset=utf-8", result as string);
    }
    return reply(r, response, r.status, JSON_TYPE, r.method.returns.encode(result));
  } catch (e) {
    return failure(r, e, ctx);
  }
}

// what one request's handler left: the value it returned, an answer the pipes gave instead, @Res() use
class RouteRun {
  result: unknown = null;
  early: HttpResponse | null = null; // the answer (a body that is not JSON, a parameter a pipe refused)
  usesRes: boolean = false;
}

// a guard's answer: a boolean, or an Observable / a promise of one
function settleBoolean(v: unknown): boolean {
  return observed(v) as boolean;
}

// an interceptor's or a guard's value: an Observable runs and gives its value
function observed(v: unknown): unknown {
  if (v instanceof Observable) {
    return (v as Observable<unknown>).run();
  }
  return v;
}

function early(run: RouteRun, res: HttpResponse): unknown {
  run.early = res;
  return null;
}

// the parameters (pipes) and the call of the route's method
function runHandler(r: Route, req: HttpRequest, request: Request, response: Response, ctx: ExecutionContext, run: RouteRun): unknown {
  const args: unknown[] = [];
  for (let i: int = 0; i < r.method.params.length; i++) {
    args.push(null);
  }
  for (const p of r.params) {
    const type = r.method.params[p.index].type;
    if (p.source === "body") {
      let text = req.body;
      if (p.key.length > 0) {
        text = bodyField(req.body, p.key);
      }
      if (text.length === 0) {
        text = "{}"; // Nest's ValidationPipe validates a missing body as an empty object
      }
      let value: unknown = null;
      try {
        value = type.decode(text);
      } catch (je) {
        return early(run, badRequestText(r, response, je.message));
      }
      if (r.validate && type.kind === "class") {
        const messages = validationMessages(value, type.id, text);
        if (messages.length > 0) {
          return early(run, badRequest(r, response, messages));
        }
      }
      args[p.index] = value;
    } else if (p.source === "param" || p.source === "query") {
      const raw = p.source === "param" ? req.param(p.key) : req.query(p.key);
      if (p.pipe === "ParseIntPipe") {
        if (!isInt(raw)) {
          return early(run, badRequestText(r, response, "Validation failed (numeric string is expected)"));
        }
        args[p.index] = parseInt(raw, 10);
      } else if (p.pipe === "ParseFloatPipe") {
        const n = Number(raw);
        if (raw.length === 0 || isNaN(n)) {
          return early(run, badRequestText(r, response, "Validation failed (numeric string is expected)"));
        }
        args[p.index] = n;
      } else if (p.pipe === "ParseBoolPipe") {
        if (raw !== "true" && raw !== "false") {
          return early(run, badRequestText(r, response, "Validation failed (boolean string is expected)"));
        }
        args[p.index] = raw === "true";
      } else if (raw.length === 0 && type.kind !== "string") {
        args[p.index] = null; // (a value that is absent: Nest gives undefined)
      } else {
        args[p.index] = raw;
      }
    } else if (p.source === "headers") {
      args[p.index] = req.header(p.key);
    } else if (p.source === "req") {
      args[p.index] = request;
    } else if (p.source === "res") {
      args[p.index] = response;
      run.usesRes = true;
    } else if (p.source === "file") {
      const f = readUpload(req, r.upload);
      if (f === null) {
        args[p.index] = null;
      } else {
        args[p.index] = f;
      }
    } else {
      args[p.index] = __custom[p.custom].fn(p.key === "" ? null : p.key, ctx); // @Dec(): data is undefined in Nest
    }
  }
  const result = r.method.invoke(r.instance, args);
  run.result = result;
  return result;
}

// guards or interceptors of one route, in the order they run
class Stage {
  instances: unknown[] = [];
  methods: MethodInfo[] = [];
  add(instance: unknown, c: ClassInfo, method: string): void {
    const m = findMethod(c, method);
    if (m === null) {
      throw new Error(`${c.name} has no ${method}()`);
    }
    this.instances.push(instance);
    this.methods.push(m);
  }
  copy(): Stage {
    const s = new Stage();
    s.instances = this.instances.slice(0);
    s.methods = this.methods.slice(0);
    return s;
  }
}

// a method of the class or one it inherits (from a base class with a ClassInfo: AuthGuard's canActivate)
export function findMethod(c: ClassInfo, name: string): MethodInfo | null {
  let k: ClassInfo | null = c;
  while (k !== null) {
    const m = k.methods.find((x: MethodInfo): boolean => x.name === name);
    if (m !== null) {
      return m;
    }
    k = k.base;
  }
  return null;
}

function hasMethod(c: ClassInfo, name: string): boolean {
  return findMethod(c, name) !== null;
}

// the class a provider builds: the class itself, or { useClass }
function providerClass(p: unknown): ClassInfo | null {
  if (p instanceof ClassProvider) {
    return (p as ClassProvider).useClass;
  }
  return asClass(p);
}

// a guard's or an interceptor's instance: the provider's when the class is one (one instance, as in Nest's
// injector), built with its dependencies otherwise
function instanceOf(c: ClassInfo): unknown {
  const key = tokenKey(c);
  if (container.entries.has(key)) {
    return resolveToken(c, false, c.name, 0, 0);
  }
  return construct(c, 0);
}

function lifecycle(name: string): void {
  for (const w of container.wrappers) {
    const m = w.metatype;
    if (m === null || w.instance === null) {
      continue;
    }
    const f = m.methods.find((x: MethodInfo): boolean => x.name === name);
    if (f !== null) {
      f.invoke(w.instance, []);
    }
  }
}

/** The application NestFactory.create gives: configure it, then listen. */
export class NestApplication {
  private readonly root: ClassInfo;
  private cors: boolean;
  private corsOptions: CorsOptions | null = null;
  private prefix: string = "";
  private readonly extras: ExtraRoute[] = [];
  private versioning: boolean = false;
  private defaultVersion: string = "";
  private versionPrefix: string = "v";
  private validateAll: boolean = false;
  private readonly middleware: RequestHandler[] = [];
  private readonly globalFilters: unknown[] = [];
  private initialized: boolean = false;

  constructor(root: ClassInfo, cors: boolean) {
    this.root = root;
    this.cors = cors;
  }

  setGlobalPrefix(prefix: string): void {
    this.prefix = prefix;
  }

  getGlobalPrefix(): string {
    return this.prefix;
  }

  getRootModule(): ClassInfo {
    return this.root;
  }

  /** URI versioning: @Version("1") routes are served under /v1. */
  enableVersioning(options: VersioningOptions): NestApplication {
    this.versioning = options.type === VersioningType.URI;
    this.defaultVersion = options.defaultVersion || "";
    if (options.prefix !== null) {
      this.versionPrefix = options.prefix || "v";
    }
    return this;
  }

  /** CORS on every route; the options are accepted (the answer allows any origin, as before) */
  enableCors(options: CorsOptions | null = null): void {
    this.cors = true;
    this.corsOptions = options;
  }

  /** A middleware for every request (helmet(), compression()). */
  use(handler: RequestHandler): NestApplication {
    this.middleware.push(handler);
    return this;
  }

  /** new ValidationPipe() for every route: class bodies are validated. */
  useGlobalPipes(pipe: ValidationPipe): NestApplication {
    this.validateAll = true;
    return this;
  }

  /** An exception filter for every route (register it as { provide: APP_FILTER, useClass: MyFilter } instead: accepted, not applied). */
  useGlobalFilters(filter: unknown): NestApplication {
    this.globalFilters.push(filter);
    return this;
  }

  /** Sends Nest's log lines to another logger: nestjs-pino's Logger takes them over as soon as it is built, so this only accepts it. */
  useLogger(logger: unknown): void {}

  /** Runs onModuleDestroy and onApplicationShutdown on exit (accepted; xisto runs them on close()). */
  enableShutdownHooks(signals: string[] = [], options: ShutdownHooksOptions | null = null): NestApplication {
    return this;
  }

  /** The instance of a provider. */
  get(token: unknown): unknown {
    this.init();
    return getInstance(token);
  }

  // xisto: a GET route served next to the controllers' (what Nest does through getHttpAdapter())
  addGetRoute(path: string, handler: (req: HttpRequest) => HttpResponse): void {
    this.extras.push({ path: joinPath([path]), handler: handler });
  }

  /** Builds the modules and providers (listen does it too). */
  init(): void {
    if (this.initialized) {
      return;
    }
    this.initialized = true;
    ensureCollected(this.root);
    for (const f of __appFactories) {
      __values.set(f.provide as string, build(f, 0)); // (forRootAsync with inject: its factory sees the providers)
    }
    for (const e of container.entries.values()) {
      resolveToken(e.token, false, "application", 0, 0);
    }
    lifecycle("onModuleInit");
  }

  async listen(port: number, host: string = ""): Promise<void> {
    this.init();
    const server = new Server();
    const filterInstances: unknown[] = [];
    const filterInfos: ClassInfo[] = [];
    const globalGuards = new Stage();
    const globalInterceptors = new Stage();
    for (const g of container.globals) {
      const c = providerClass(g);
      if (c === null) {
        continue;
      }
      if (__catches.has(c.id)) {
        filterInstances.push(build(g, 0));
        filterInfos.push(c);
      } else if (hasMethod(c, "canActivate")) {
        globalGuards.add(build(g, 0), c, "canActivate");
      } else if (hasMethod(c, "intercept")) {
        globalInterceptors.add(build(g, 0), c, "intercept");
      }
    }
    for (const c of container.controllers) {
      const instance = construct(c, 0);
      container.wrappers.push({ name: c.name, token: c, instance: instance, metatype: c, host: null });
      const base0 = __controllers.get(c.id);
      const routes0 = __routes.get(c.id);
      const params0 = __params.get(c.id);
      const guards0 = __guards.get(c.id);
      const base = base0 || "";
      const routes: RouteDef[] = routes0 || [];
      const params: ParamDef[] = params0 || [];
      const guards: GuardDef[] = guards0 || [];
      const validated0 = __validated.get(c.id);
      const validated: int[] = validated0 || [];
      for (const rd of routes) {
        const mine: ParamDef[] = params.filter((p: ParamDef): boolean => p.method === rd.method);
        // guards and interceptors: the global ones, the controller's, the route's (each in the decorator's order)
        const gs = globalGuards.copy();
        for (const level of [-1, rd.method]) {
          for (const g of guards) {
            if (g.method === level) {
              gs.add(instanceOf(g.guard), g.guard, "canActivate");
            }
          }
        }
        const ins = globalInterceptors.copy();
        let upload = "";
        for (const level of [-1, rd.method]) {
          for (const s of settingsOf(c, rd.method, "interceptor")) {
            if (s.method !== level) {
              continue;
            }
            const f = uploadField(s.value);
            if (f.length > 0) {
              upload = f;
            } else {
              const ic = s.value as ClassInfo;
              ins.add(instanceOf(ic), ic, "intercept");
            }
          }
        }
        let status: int = rd.verb === "POST" ? 201 : 200;
        for (const s of settingsOf(c, rd.method, "code")) {
          status = s.code;
        }
        // the route's versions: the method's own, else the controller's, else the default (one route per version)
        const mine_v: string[] = [];
        const cls_v: string[] = [];
        for (const s of settingsOf(c, rd.method, "version")) {
          if (s.method === rd.method) {
            mine_v.push(s.text);
          } else {
            cls_v.push(s.text);
          }
        }
        const versions: string[] = mine_v.length > 0 ? mine_v : cls_v.length > 0 ? cls_v : [this.defaultVersion];
        const headers: string[] = [];
        for (const s of settingsOf(c, rd.method, "header")) {
          headers.push(s.text);
          headers.push(s.value as string);
        }
        const fis: unknown[] = [];
        const finfo: ClassInfo[] = [];
        for (const s of settingsOf(c, rd.method, "filter")) {
          const fc = s.cls;
          if (fc !== null) {
            fis.push(construct(fc, 0));
            finfo.push(fc);
          }
        }
        for (let i = 0; i < filterInstances.length; i++) {
          fis.push(filterInstances[i]);
          finfo.push(filterInfos[i]);
        }
        for (const version of versions) {
        const v = this.versioning && version.length > 0 && version !== VERSION_NEUTRAL ? this.versionPrefix + version : "";
        const r: Route = {
          verb: rd.verb,
          path: joinPath([this.prefix, v, base, rd.path]),
          instance: instance,
          method: c.methods[rd.method],
          handler: new RouteHandler(c, rd.method, c.methods[rd.method].name),
          interceptors: ins.instances,
          interceptorMethods: ins.methods,
          params: mine,
          guards: gs.instances,
          guardMethods: gs.methods,
          validate: this.validateAll || validated.includes(rd.method),
          status: status,
          cors: this.cors,
          headers: headers,
          filters: fis,
          filterInfos: finfo,
          upload: upload,
          middleware: this.middleware,
        };
        const h = (req: HttpRequest): HttpResponse => handle(r, req);
        if (rd.verb === "GET") {
          server.get(r.path, h);
        } else if (rd.verb === "POST") {
          server.post(r.path, h);
        } else if (rd.verb === "PUT" || rd.verb === "PATCH") {
          server.put(r.path, h);
        } else {
          server.delete(r.path, h);
        }
        }
      }
    }
    for (const x of this.extras) {
      server.get(x.path, x.handler);
    }
    lifecycle("onApplicationBootstrap");
    new Logger("NestApplication").log("Nest application successfully started");
    server.listen(Math.trunc(port));
  }

  /** Runs onModuleDestroy and onApplicationShutdown. */
  async close(): Promise<void> {
    lifecycle("onModuleDestroy");
    lifecycle("beforeApplicationShutdown");
    lifecycle("onApplicationShutdown");
  }
}

/** NestFactory.create(AppModule, options?): the application, ready to configure and listen. */
export class NestFactory {
  static create(module: ClassInfo, options: NestApplicationOptions = {}): NestApplication {
    return new NestApplication(module, options.cors || false);
  }
}
