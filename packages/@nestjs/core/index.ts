// @nestjs/core for xisto: NestFactory.create(AppModule) walks the modules @Module recorded, builds every
// provider and controller once (constructor parameters by type, like Nest's injector), and registers each
// route on xisto's HTTP server with guards, parameter decorators, the ValidationPipe and Nest's error bodies.
import { ClassInfo, MethodInfo } from "xisto:reflect";
import { Request } from "express";
import { validationMessages } from "class-validator";
import {
  __classes,
  __controllers,
  __custom,
  __guards,
  __modules,
  __params,
  __routes,
  __validated,
  __jsonValues,
  __tokens,
  __values,
  bodyField,
  ExecutionContext,
  GuardDef,
  HttpException,
  ModuleMetadata,
  ParamDef,
  RouteDef,
} from "@nestjs/common";

export interface NestApplicationOptions {
  cors?: boolean;
}

interface Route {
  verb: string;
  path: string;
  instance: unknown;
  method: MethodInfo;
  params: ParamDef[];
  guards: unknown[];
  guardMethods: MethodInfo[];
  validate: boolean;
  status: int;
  cors: boolean;
}

const JSON_TYPE = "application/json; charset=utf-8";

// a GET route outside the controllers (SwaggerModule.setup serves the API documentation through one)
interface ExtraRoute {
  path: string;
  handler: (req: HttpRequest) => HttpResponse;
}

// the controllers the root module reaches through its imports, in Nest's order
export function moduleControllers(root: ClassInfo): ClassInfo[] {
  const providers: Map<string, ClassInfo> = new Map<string, ClassInfo>();
  const controllers: ClassInfo[] = [];
  collect(root.id, providers, controllers, new Map<string, boolean>());
  return controllers;
}

function collect(id: string, providers: Map<string, ClassInfo>, controllers: ClassInfo[], seen: Map<string, boolean>): void {
  if (seen.has(id)) {
    return;
  }
  seen.set(id, true);
  const meta = __modules.get(id);
  if (meta === null) {
    throw new Error(`Nest can't find the module ${id}: is it decorated with @Module()?`);
  }
  const imports = meta.imports || [];
  for (const m of imports) {
    collect(m.id, providers, controllers, seen);
  }
  const provided = meta.providers || [];
  for (const p of provided) {
    providers.set(p.id, p);
  }
  const ctrls = meta.controllers || [];
  for (const c of ctrls) {
    controllers.push(c);
  }
}

// one instance per provider, its constructor parameters resolved by type first
function resolve(ci: ClassInfo, providers: Map<string, ClassInfo>, instances: Map<string, unknown>, depth: int): unknown {
  const have = instances.get(ci.id);
  if (have !== null) {
    return have;
  }
  if (depth > 32) {
    throw new Error(`Nest can't resolve dependencies of ${ci.name}: circular dependency`);
  }
  const args: unknown[] = [];
  for (const p of ci.params) {
    // a token (@Inject, @InjectRepository) or a value registered for the type: what the parameter receives
    const token = __tokens.get(ci.id + "#" + String(p.index)) ?? p.type.id;
    const value = __values.get(token);
    if (value !== null) {
      args.push(value);
      continue;
    }
    const json = __jsonValues.get(token);
    if (json !== null) {
      args.push(p.type.decode(json));
      continue;
    }
    const dep = providers.get(p.type.id);
    if (dep === null) {
      throw new Error(`Nest can't resolve dependencies of the ${ci.name} (${token} at index [${p.index}]). Make sure it is available in the current module.`);
    }
    args.push(resolve(dep, providers, instances, depth + 1));
  }
  const made = ci.construct(args);
  instances.set(ci.id, made);
  return made;
}

// "/" + the non-empty parts without their slashes: the path Nest gives prefix + controller + route
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

function reply(r: Route, status: int, contentType: string, body: string): HttpResponse {
  const res = HttpResponse.of(status, contentType, body);
  if (r.cors) {
    return res.header("Access-Control-Allow-Origin", "*");
  }
  return res;
}

function badRequest(r: Route, messages: string[]): HttpResponse {
  return reply(r, 400, JSON_TYPE, `{"statusCode":400,"message":${JSON.stringify(messages)},"error":"Bad Request"}`);
}

function failure(r: Route, e: Error): HttpResponse {
  const cause = e.cause;
  if (cause instanceof HttpException) {
    const h = cause as HttpException;
    const text = JSON.stringify(h.response);
    if (text.startsWith("\"")) {
      return reply(r, h.status, JSON_TYPE, `{"statusCode":${h.status},"message":${text}}`);
    }
    return reply(r, h.status, JSON_TYPE, text);
  }
  console.log(`[Nest] ERROR [ExceptionsHandler] ${e.message}\n${e.stack}`);
  return reply(r, 500, JSON_TYPE, `{"statusCode":500,"message":"Internal server error"}`);
}

function handle(r: Route, req: HttpRequest): HttpResponse {
  const auth = req.header("authorization");
  let header: string | null = null;
  if (auth.length > 0) {
    header = auth;
  }
  const request: Request = { method: req.method, url: req.path, headers: { authorization: header }, body: req.body };
  const ctx = new ExecutionContext(request);
  try {
    for (let g: int = 0; g < r.guards.length; g++) {
      const ok = r.guardMethods[g].invoke(r.guards[g], [ctx]) as boolean;
      if (!ok) {
        return reply(r, 403, JSON_TYPE, `{"statusCode":403,"message":"Forbidden resource","error":"Forbidden"}`);
      }
    }
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
          return reply(r, 400, JSON_TYPE, `{"statusCode":400,"message":${JSON.stringify(je.message)},"error":"Bad Request"}`);
        }
        if (r.validate && type.kind === "class") {
          const messages = validationMessages(value, type.id);
          if (messages.length > 0) {
            return badRequest(r, messages);
          }
        }
        args[p.index] = value;
      } else if (p.source === "param") {
        args[p.index] = req.param(p.key);
      } else if (p.source === "query") {
        args[p.index] = req.query(p.key);
      } else if (p.source === "headers") {
        args[p.index] = req.header(p.key);
      } else {
        args[p.index] = __custom[p.custom].fn(p.key, ctx);
      }
    }
    const result = r.method.invoke(r.instance, args);
    if (r.method.returns.kind === "void" || result === null) {
      return reply(r, r.status, "text/html; charset=utf-8", "");
    }
    if (r.method.returns.kind === "string") {
      return reply(r, r.status, "text/html; charset=utf-8", result as string);
    }
    return reply(r, r.status, JSON_TYPE, r.method.returns.encode(result));
  } catch (e) {
    return failure(r, e);
  }
}

export class NestApplication {
  private readonly root: ClassInfo;
  private readonly cors: boolean;
  private prefix: string = "";
  private readonly extras: ExtraRoute[] = [];

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

  // xisto: a GET route served next to the controllers' (what Nest does through getHttpAdapter())
  addGetRoute(path: string, handler: (req: HttpRequest) => HttpResponse): void {
    this.extras.push({ path: joinPath([path]), handler: handler });
  }

  async listen(port: int): Promise<void> {
    const providers: Map<string, ClassInfo> = new Map<string, ClassInfo>();
    const controllers: ClassInfo[] = [];
    collect(this.root.id, providers, controllers, new Map<string, boolean>());
    const instances: Map<string, unknown> = new Map<string, unknown>();
    const server = new Server();
    for (const c of controllers) {
      const instance = resolve(c, providers, instances, 0);
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
        const gs: unknown[] = [];
        const gms: MethodInfo[] = [];
        for (const g of guards) {
          if (g.method !== rd.method) {
            continue;
          }
          const gi = resolve(g.guard, providers, instances, 0);
          gs.push(gi);
          const m = g.guard.methods.find((x: MethodInfo): boolean => x.name === "canActivate");
          if (m === null) {
            throw new Error(`${g.guard.name} has no canActivate()`);
          }
          gms.push(m);
        }
        const r: Route = {
          verb: rd.verb,
          path: joinPath([this.prefix, base, rd.path]),
          instance: instance,
          method: c.methods[rd.method],
          params: mine,
          guards: gs,
          guardMethods: gms,
          validate: validated.includes(rd.method),
          status: rd.verb === "POST" ? 201 : 200,
          cors: this.cors,
        };
        const h = (req: HttpRequest): HttpResponse => handle(r, req);
        if (rd.verb === "GET") {
          server.get(r.path, h);
        } else if (rd.verb === "POST") {
          server.post(r.path, h);
        } else if (rd.verb === "PUT") {
          server.put(r.path, h);
        } else {
          server.delete(r.path, h);
        }
      }
    }
    for (const x of this.extras) {
      server.get(x.path, x.handler);
    }
    server.listen(port);
  }
}

export interface NestFactoryStatic {
  create: (module: ClassInfo, options: NestApplicationOptions) => NestApplication;
}

export const NestFactory: NestFactoryStatic = {
  create: (module: ClassInfo, options: NestApplicationOptions): NestApplication => new NestApplication(module, options.cors || false),
};
