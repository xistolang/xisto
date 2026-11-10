// @nestjs/swagger for xisto: the OpenAPI 3.0 document of a Nest application, built from what the @nestjs/common
// decorators recorded and from the types the compiler knows (TypeInfo.schema). DTOs need no @ApiProperty: the
// shape of every body, parameter and response is its TypeScript type, as the Nest CLI plugin would derive it.
import { ClassInfo, FieldInfo, MethodInfo } from "xisto:reflect";
import { __controllers, __guards, __params, __routes, GuardDef, ParamDef, RouteDef } from "@nestjs/common";
import { joinPath, moduleControllers, NestApplication } from "@nestjs/core";

const JSON_TYPE = "application/json; charset=utf-8";
const UI_VERSION = "5.17.14"; // swagger-ui-dist served by the docs page

export interface SwaggerConfig {
  title: string;
  description: string;
  version: string;
  bearer: boolean;
}

export class DocumentBuilder {
  private title: string = "";
  private description: string = "";
  private version: string = "1.0.0";
  private bearer: boolean = false;

  setTitle(title: string): DocumentBuilder {
    this.title = title;
    return this;
  }
  setDescription(description: string): DocumentBuilder {
    this.description = description;
    return this;
  }
  setVersion(version: string): DocumentBuilder {
    this.version = version;
    return this;
  }
  addBearerAuth(): DocumentBuilder {
    this.bearer = true;
    return this;
  }
  build(): SwaggerConfig {
    return { title: this.title, description: this.description, version: this.version, bearer: this.bearer };
  }
}

// the document, as the JSON text served at <path>-json
export class OpenAPIObject {
  readonly json: string;
  constructor(json: string) {
    this.json = json;
  }
}

// ---- optional decorators: what the types cannot say ----
export interface ApiOperationOptions {
  summary?: string;
  description?: string;
}
export class ApiResponseOptions {
  status: int;
  description?: string;
  type: unknown = null;
  isArray?: boolean;
}
interface OperationDoc {
  method: string;
  summary: string;
  description: string;
}
interface ResponseDoc {
  method: string;
  status: int;
  description: string;
}

const tagsOf: Map<string, string[]> = new Map<string, string[]>();
const bearerOf: Map<string, boolean> = new Map<string, boolean>();
const operationsOf: Map<string, OperationDoc[]> = new Map<string, OperationDoc[]>();
const responsesOf: Map<string, ResponseDoc[]> = new Map<string, ResponseDoc[]>();

// the mapped types: class UpdateDto extends PartialType(CreateDto) {} copies CreateDto's fields (with their
// decorators) into UpdateDto, each optional; OmitType(A, ["k"] as const) leaves keys out, PickType(A, ["k"]) keeps
// only them, IntersectionType(A, B) joins two. The compiler does the copying: these are the names to import.
export function PartialType(cls: ClassInfo): ClassInfo {
  return cls;
}
export function OmitType(cls: ClassInfo, keys: string[]): ClassInfo {
  return cls;
}
export function PickType(cls: ClassInfo, keys: string[]): ClassInfo {
  return cls;
}
export function IntersectionType(a: ClassInfo, b: ClassInfo): ClassInfo {
  return a;
}

export function ApiTags(tag: string): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    const list = tagsOf.get(c.id);
    if (list === null) {
      tagsOf.set(c.id, [tag]);
    } else {
      list.push(tag);
    }
  };
}

// every route of the class is documented as needing the token (routes behind a guard already are)
export function ApiBearerAuth(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    bearerOf.set(c.id, true);
  };
}

export function ApiOperation(o: ApiOperationOptions): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {
    const d: OperationDoc = { method: m.name, summary: o.summary || "", description: o.description || "" };
    const list = operationsOf.get(c.id);
    if (list === null) {
      operationsOf.set(c.id, [d]);
    } else {
      list.push(d);
    }
  };
}

export function ApiResponse(o: ApiResponseOptions): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {
    const d: ResponseDoc = { method: m.name, status: o.status, description: o.description || "" };
    const list = responsesOf.get(c.id);
    if (list === null) {
      responsesOf.set(c.id, [d]);
    } else {
      list.push(d);
    }
  };
}

/** Options of @ApiProperty / @ApiPropertyOptional (the schema comes from the field's type; these are accepted). */
export class ApiPropertyOptions {
  description?: string;
  example: unknown = null;
  required?: boolean;
  type: unknown = null;
  enum: unknown = null;
  isArray?: boolean;
  format?: string;
  nullable?: boolean;
  default: unknown = null;
  minimum?: number;
  maximum?: number;
  maxLength?: int;
  minLength?: int;
  deprecated?: boolean;
}

/** Documents a DTO field (its schema is the field's TypeScript type). */
export function ApiProperty(o: ApiPropertyOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {};
}

/** Documents an optional DTO field. */
export function ApiPropertyOptional(o: ApiPropertyOptions = {}): (c: ClassInfo, f: FieldInfo) => void {
  return (c: ClassInfo, f: FieldInfo): void => {};
}

/** Options of @ApiBody, @ApiParam and @ApiQuery (accepted: the route's types already describe them). */
export class ApiParamOptions {
  name?: string;
  description?: string;
  required?: boolean;
  type: unknown = null;
  example: unknown = null;
  enum: unknown = null;
  schema: unknown = null;
  isArray?: boolean;
}

export function ApiBody(o: ApiParamOptions = {}): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {};
}
export function ApiParam(o: ApiParamOptions = {}): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {};
}
export function ApiQuery(o: ApiParamOptions = {}): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {};
}
/** The media types a route accepts (accepted). */
export function ApiConsumes(mime: string): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {};
}
/** The media types a route answers with (accepted). */
export function ApiProduces(mime: string): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {};
}

// ---- the document ----
const DEFS = ',"$defs":{\n';

// the schema with its definitions moved to components.schemas: the compiler writes each one on its own line
function lift(schema: string, defs: Map<string, string>): string {
  const s = schema.replaceAll("#/$defs/", "#/components/schemas/");
  const at = s.indexOf(DEFS);
  if (at < 0) {
    return s;
  }
  const entries = s.slice(at + DEFS.length, s.length - 2).split("\n");
  for (const line of entries) {
    let e = line;
    if (e.endsWith(",")) {
      e = e.slice(0, e.length - 1);
    }
    const colon = e.indexOf('":');
    const name = e.slice(1, colon);
    if (!defs.has(name)) {
      defs.set(name, e.slice(colon + 2, e.length));
    }
  }
  return s.slice(0, at) + "}";
}

// /articles/:slug -> /articles/{slug}, and the names of its path parameters
function openApiPath(path: string, names: string[]): string {
  const segments = path.split("/");
  const out: string[] = [];
  for (const seg of segments) {
    if (seg.startsWith(":")) {
      const name = seg.slice(1, seg.length);
      names.push(name);
      out.push("{" + name + "}");
    } else {
      out.push(seg);
    }
  }
  return out.join("/");
}

function quote(s: string): string {
  return JSON.stringify(s);
}

function operation(c: ClassInfo, rd: RouteDef, path: string, pathNames: string[], params: ParamDef[], guarded: boolean, bearer: boolean, defs: Map<string, string>): string {
  const m = c.methods[rd.method];
  const parts: string[] = [];
  parts.push(`"operationId":${quote(c.name + "_" + m.name)}`);
  const ops = operationsOf.get(c.id);
  if (ops !== null) {
    const o = ops.find((x: OperationDoc): boolean => x.method === m.name);
    if (o !== null) {
      if (o.summary.length > 0) {
        parts.push(`"summary":${quote(o.summary)}`);
      }
      if (o.description.length > 0) {
        parts.push(`"description":${quote(o.description)}`);
      }
    }
  }
  const tags = tagsOf.get(c.id);
  if (tags !== null) {
    parts.push(`"tags":${JSON.stringify(tags)}`);
  }
  const ps: string[] = [];
  const declared: string[] = [];
  let body = "";
  for (const p of params) {
    if (p.method !== rd.method) {
      continue;
    }
    const schema = lift(m.params[p.index].type.schema, defs);
    if (p.source === "param" && p.key.length > 0) {
      declared.push(p.key);
      ps.push(`{"name":${quote(p.key)},"required":true,"in":"path","schema":${schema}}`);
    } else if (p.source === "query" && p.key.length > 0) {
      ps.push(`{"name":${quote(p.key)},"required":false,"in":"query","schema":${schema}}`);
    } else if (p.source === "headers" && p.key.length > 0) {
      ps.push(`{"name":${quote(p.key)},"required":false,"in":"header","schema":${schema}}`);
    } else if (p.source === "body") {
      let s = schema;
      if (p.key.length > 0) {
        s = `{"type":"object","properties":{${quote(p.key)}:${schema}},"required":[${quote(p.key)}]}`;
      }
      body = `"requestBody":{"required":true,"content":{"application/json":{"schema":${s}}}}`;
    }
  }
  for (const name of pathNames) {
    if (!declared.includes(name)) {
      ps.push(`{"name":${quote(name)},"required":true,"in":"path","schema":{"type":"string"}}`);
    }
  }
  parts.push(`"parameters":[${ps.join(",")}]`);
  if (body.length > 0) {
    parts.push(body);
  }
  const status: int = rd.verb === "POST" ? 201 : 200;
  let described = rd.verb === "POST" ? "Created" : "OK";
  const others: string[] = [];
  const docs = responsesOf.get(c.id);
  if (docs !== null) {
    for (const d of docs) {
      if (d.method !== m.name) {
        continue;
      }
      if (d.status === status) {
        if (d.description.length > 0) {
          described = d.description;
        }
      } else {
        others.push(`"${d.status}":{"description":${quote(d.description)}}`);
      }
    }
  }
  let content = "";
  if (m.returns.kind === "string") {
    content = `,"content":{"text/html":{"schema":{"type":"string"}}}`;
  } else if (m.returns.kind !== "void") {
    content = `,"content":{"application/json":{"schema":${lift(m.returns.schema, defs)}}}`;
  }
  const rs: string[] = [`"${status}":{"description":${quote(described)}${content}}`];
  for (const o of others) {
    rs.push(o);
  }
  parts.push(`"responses":{${rs.join(",")}}`);
  if (bearer && (guarded || bearerOf.has(c.id))) {
    parts.push(`"security":[{"bearer":[]}]`);
  }
  return `"${rd.verb.toLowerCase()}":{${parts.join(",")}}`;
}

export interface SwaggerModuleStatic {
  createDocument: (app: NestApplication, config: SwaggerConfig) => OpenAPIObject;
  setup: (path: string, app: NestApplication, document: OpenAPIObject) => void;
}

function createDocument(app: NestApplication, config: SwaggerConfig): OpenAPIObject {
  const defs: Map<string, string> = new Map<string, string>();
  const paths: Map<string, string[]> = new Map<string, string[]>();
  const order: string[] = [];
  const tagNames: string[] = [];
  for (const c of moduleControllers(app.getRootModule())) {
    const base0 = __controllers.get(c.id);
    const routes0 = __routes.get(c.id);
    const params0 = __params.get(c.id);
    const guards0 = __guards.get(c.id);
    const base = base0 || "";
    const routes: RouteDef[] = routes0 || [];
    const params: ParamDef[] = params0 || [];
    const guards: GuardDef[] = guards0 || [];
    const tags = tagsOf.get(c.id);
    if (tags !== null) {
      for (const t of tags) {
        if (!tagNames.includes(t)) {
          tagNames.push(t);
        }
      }
    }
    for (const rd of routes) {
      const names: string[] = [];
      const path = openApiPath(joinPath([app.getGlobalPrefix(), base, rd.path]), names);
      const guarded = guards.some((g: GuardDef): boolean => g.method === rd.method);
      const op = operation(c, rd, path, names, params, guarded, config.bearer, defs);
      const list = paths.get(path);
      if (list === null) {
        paths.set(path, [op]);
        order.push(path);
      } else {
        list.push(op);
      }
    }
  }
  const ps: string[] = [];
  for (const p of order) {
    const ops0 = paths.get(p);
    const ops: string[] = ops0 || [];
    ps.push(`${quote(p)}:{${ops.join(",")}}`);
  }
  const ss: string[] = [];
  for (const name of defs.keys()) {
    const def = defs.get(name);
    ss.push(`${quote(name)}:${def || "{}"}`);
  }
  const ts: string[] = tagNames.map((t: string): string => `{"name":${quote(t)},"description":""}`);
  let security = "";
  if (config.bearer) {
    security = `"securitySchemes":{"bearer":{"scheme":"bearer","bearerFormat":"JWT","type":"http"}},`;
  }
  const info = `{"title":${quote(config.title)},"description":${quote(config.description)},"version":${quote(config.version)},"contact":{}}`;
  return new OpenAPIObject(`{"openapi":"3.0.0","paths":{${ps.join(",")}},"info":${info},"tags":[${ts.join(",")}],"servers":[],"components":{${security}"schemas":{${ss.join(",")}}}}`);
}

function page(title: string, jsonPath: string): string {
  return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${title}</title>
<link rel="stylesheet" href="https://unpkg.com/swagger-ui-dist@${UI_VERSION}/swagger-ui.css">
</head>
<body>
<div id="swagger-ui"></div>
<script src="https://unpkg.com/swagger-ui-dist@${UI_VERSION}/swagger-ui-bundle.js"></script>
<script>
window.ui = SwaggerUIBundle({ url: ${quote(jsonPath)}, dom_id: "#swagger-ui", persistAuthorization: true });
</script>
</body>
</html>
`;
}

// SwaggerModule.setup("docs", app, document): the UI at /docs and the document at /docs-json, outside the global
// prefix, as in Nest
function setup(path: string, app: NestApplication, document: OpenAPIObject): void {
  const base = joinPath([path]);
  const json = document.json;
  const html = page("Swagger UI", base + "-json");
  app.addGetRoute(base, (req: HttpRequest): HttpResponse => HttpResponse.of(200, "text/html; charset=utf-8", html));
  app.addGetRoute(base + "-json", (req: HttpRequest): HttpResponse => HttpResponse.of(200, JSON_TYPE, json));
}

export const SwaggerModule: SwaggerModuleStatic = {
  createDocument: createDocument,
  setup: setup,
};
