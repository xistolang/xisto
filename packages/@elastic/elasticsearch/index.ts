// @elastic/elasticsearch (contract ^8): the client for xisto, over fetch.
//
// As in the version 8 client, a call takes one object of parameters and returns the response body. Each call
// knows which of its parameters go to the path (index, id, name) and to the query string (refresh, routing...);
// the rest is the body. Responses are `object` (JSON): give them a type where they are read
// (`const r: SearchResult = await client.search({ ... })`).
//
// Compatibility with npm's client:
// - Errors are Error values whose message is the client's (the error type, then the root causes); xisto cannot
//   extend Error, so there is no err.meta.statusCode: pass { ignore: [404] } in the options to get the body of a
//   404 instead of an error (as the npm client allows), or test the message.
// - Calls covered: search, count, index, get, update, delete, bulk, ping, info, close, cluster.health,
//   indices.create, indices.exists, indices.delete, indices.putMapping, indices.putIndexTemplate, ilm.putLifecycle.

declare function field(json: string, key: string): string;
declare function without(json: string, keys: string): string;
declare function text(raw: string): string;
declare function ndjson(raw: string): string;
declare function encode(s: string, path: boolean): string;

export interface BasicAuth {
  username?: string;
  password?: string;
  /** An API key (the base64 "id:key" form): sent as "ApiKey <key>". */
  apiKey?: string;
  /** A bearer token: sent as "Bearer <token>". */
  bearer?: string;
}

export interface ClientOptions {
  /** The cluster's URL, as "http://host:9200" (a trailing slash is fine). */
  node: string;
  auth?: BasicAuth;
  /** Milliseconds before a request is given up (default 30 000). */
  requestTimeout?: number;
  /** How many times a request is retried after a connection error, a timeout or a 502/503/504 (default 3). */
  maxRetries?: number;
}

export interface TransportRequestOptions {
  requestTimeout?: number;
  maxRetries?: number;
  /** Status codes that return the body instead of throwing (as [404] for a document that may not exist). */
  ignore?: int[];
}

const SEARCH_QUERY = "routing,preference,scroll,search_type,request_cache,ignore_unavailable,allow_no_indices,expand_wildcards,timeout,"
  + "rest_total_hits_as_int,typed_keys,batched_reduce_size,max_concurrent_shard_requests,pre_filter_shard_size,ccs_minimize_roundtrips,"
  + "allow_partial_search_results,q,df,analyzer,default_operator,lenient,analyze_wildcard";
const WRITE_QUERY = "refresh,routing,timeout,version,version_type,if_seq_no,if_primary_term,wait_for_active_shards";

/** The query string of a call: each listed key present in params, as ?k=v&k2=v2 ("" when none). */
function queryString(params: string, keys: string): string {
  let q = "";
  for (const k of keys.split(",")) {
    const raw = field(params, k);
    if (raw === "") continue;
    const v = text(raw);
    q += `${q === "" ? "?" : "&"}${k}=${encode(v, false)}`;
  }
  return q;
}

/** A path segment from a parameter (required: an error names the call and the parameter when it is missing). */
function segment(params: string, key: string, call: string): string {
  const raw = field(params, key);
  const v = raw === "" ? "" : text(raw);
  if (v === "") throw new Error(`Missing required parameter: ${key} (${call})`);
  return encode(v, true);
}

/** An optional path segment: "/<value>" or "". */
function optSegment(params: string, key: string): string {
  const raw = field(params, key);
  const v = raw === "" ? "" : text(raw);
  return v === "" ? "" : "/" + encode(v, true);
}

/** The message of the npm client's ResponseError for a response body. */
function errorMessage(status: int, body: string): string {
  const err = field(body, "error");
  if (err !== "") {
    const type = text(field(err, "type"));
    if (type !== "") {
      let msg = type;
      const causes = field(err, "root_cause");
      if (causes !== "" && causes.startsWith("[")) {
        const lines = ndjson(causes).split("\n");
        let any = false;
        for (const c of lines) {
          if (c === "") continue;
          if (!any) { msg += "\n\tRoot causes:"; any = true; }
          msg += `\n\t\t${text(field(c, "type"))}: ${text(field(c, "reason"))}`;
        }
      }
      return msg;
    }
    if (err.startsWith('"')) return text(err);
  }
  return body !== "" ? body : `Response Error (${status})`;
}

/** The transport: one HTTP request with the client's headers, timeout and retries. */
class Transport {
  readonly base: string;
  readonly auth: string;
  readonly timeout: int;
  readonly retries: int;
  closed: boolean = false;

  constructor(options: ClientOptions) {
    let node = options.node;
    while (node.endsWith("/")) node = node.slice(0, node.length - 1);
    this.base = node;
    this.timeout = Math.trunc(options.requestTimeout ?? 30000);
    this.retries = Math.trunc(options.maxRetries ?? 3);
    let auth = "";
    const a = options.auth;
    if (a != null) {
      const key: string = a.apiKey ?? "";
      const bearer: string = a.bearer ?? "";
      const user: string = a.username ?? "";
      if (key !== "") auth = `ApiKey ${key}`;
      else if (bearer !== "") auth = `Bearer ${bearer}`;
      else if (user !== "") auth = "Basic " + Buffer.from(`${user}:${a.password ?? ""}`).toString("base64");
    }
    this.auth = auth;
  }

  /** (one HTTP request: authorization when the client has one, a body when there is one) */
  private async send(url: string, method: string, ct: string, body: string, timeout: int): Promise<Response> {
    if (this.auth !== "") {
      if (body !== "") return await fetch(url, { method: method, headers: { "content-type": ct, accept: "application/json", authorization: this.auth }, body: body, signal: AbortSignal.timeout(timeout) });
      return await fetch(url, { method: method, headers: { accept: "application/json", authorization: this.auth }, signal: AbortSignal.timeout(timeout) });
    }
    if (body !== "") return await fetch(url, { method: method, headers: { "content-type": ct, accept: "application/json" }, body: body, signal: AbortSignal.timeout(timeout) });
    return await fetch(url, { method: method, headers: { accept: "application/json" }, signal: AbortSignal.timeout(timeout) });
  }

  /** The body of the response (and its status in .status), or an error the way the npm client throws it. */
  async request(method: string, path: string, body: string, ndjsonBody: boolean, options: TransportRequestOptions | null): Promise<Reply> {
    if (this.closed) throw new Error("The client is closed");
    const timeout: int = options !== null ? Math.trunc(options.requestTimeout ?? Number(this.timeout)) : this.timeout;
    const retries: int = options !== null ? Math.trunc(options.maxRetries ?? Number(this.retries)) : this.retries;
    const ignore: int[] = options !== null ? (options.ignore ?? []) : [];
    const ct = ndjsonBody ? "application/x-ndjson" : "application/json";
    const url = this.base + path;
    let attempt: int = 0;
    for (;;) {
      let status: int = 0;
      let txt = "";
      let failure = "";
      try {
        const r: Response = await this.send(url, method, ct, body, timeout);
        status = r.status;
        txt = await r.text();
      } catch (e) {
        failure = e.message.includes("timeout") ? "Request timed out" : e.message;
      }
      const transient = failure !== "" || status === 502 || status === 503 || status === 504;
      if (transient && attempt < retries) {
        attempt++;
        continue;
      }
      if (failure !== "") throw new Error(failure);
      if (status >= 400 && ignore.findIndex((c) => c === status) < 0) {
        if (method === "HEAD" && status === 404) return new Reply(status, "");
        throw new Error(errorMessage(status, txt));
      }
      return new Reply(status, txt);
    }
    throw new Error("unreachable");
  }
}

/** (a response: its status and body text) */
class Reply {
  readonly status: int;
  readonly body: string;
  constructor(status: int, body: string) {
    this.status = status;
    this.body = body;
  }
  json(): object {
    return JSON.parse(this.body === "" ? "{}" : this.body);
  }
}

/** client.cluster */
export class Cluster {
  private readonly t: Transport;
  constructor(t: Transport) { this.t = t; }

  /** GET /_cluster/health[/{index}] */
  async health(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const q = queryString(p, "level,local,timeout,master_timeout,wait_for_status,wait_for_nodes,wait_for_active_shards,wait_for_events,wait_for_no_relocating_shards,wait_for_no_initializing_shards");
    const r = await this.t.request("GET", `/_cluster/health${optSegment(p, "index")}${q}`, "", false, options ?? null);
    return r.json();
  }
}

/** client.indices */
export class Indices {
  private readonly t: Transport;
  constructor(t: Transport) { this.t = t; }

  /** PUT /{index}: settings, mappings and aliases in the body. */
  async create(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const path = `/${segment(p, "index", "indices.create")}${queryString(p, "timeout,master_timeout,wait_for_active_shards")}`;
    return (await this.t.request("PUT", path, without(p, "index,timeout,master_timeout,wait_for_active_shards"), false, options ?? null)).json();
  }

  /** HEAD /{index}: true when it exists. */
  async exists(params: object, options?: TransportRequestOptions): Promise<boolean> {
    const p = JSON.stringify(params);
    const r = await this.t.request("HEAD", `/${segment(p, "index", "indices.exists")}`, "", false, options ?? null);
    return r.status === 200;
  }

  /** DELETE /{index} */
  async delete(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    return (await this.t.request("DELETE", `/${segment(p, "index", "indices.delete")}${queryString(p, "timeout,master_timeout,ignore_unavailable,allow_no_indices,expand_wildcards")}`, "", false, options ?? null)).json();
  }

  /** PUT /{index}/_mapping */
  async putMapping(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const keys = "timeout,master_timeout,ignore_unavailable,allow_no_indices,expand_wildcards,write_index_only";
    const path = `/${segment(p, "index", "indices.putMapping")}/_mapping${queryString(p, keys)}`;
    return (await this.t.request("PUT", path, without(p, "index," + keys), false, options ?? null)).json();
  }

  /** PUT /_index_template/{name} */
  async putIndexTemplate(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const path = `/_index_template/${segment(p, "name", "indices.putIndexTemplate")}${queryString(p, "create,master_timeout,cause")}`;
    return (await this.t.request("PUT", path, without(p, "name,create,master_timeout,cause"), false, options ?? null)).json();
  }
}

/** client.ilm */
export class Ilm {
  private readonly t: Transport;
  constructor(t: Transport) { this.t = t; }

  /** PUT /_ilm/policy/{name} */
  async putLifecycle(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const path = `/_ilm/policy/${segment(p, "name", "ilm.putLifecycle")}${queryString(p, "master_timeout,timeout")}`;
    return (await this.t.request("PUT", path, without(p, "name,master_timeout,timeout"), false, options ?? null)).json();
  }
}

/** The Elasticsearch client (version 8 API): each call returns the response body. */
export class Client {
  readonly cluster: Cluster;
  readonly indices: Indices;
  readonly ilm: Ilm;
  private readonly t: Transport;

  constructor(options: ClientOptions) {
    this.t = new Transport(options);
    this.cluster = new Cluster(this.t);
    this.indices = new Indices(this.t);
    this.ilm = new Ilm(this.t);
  }

  /** POST /{index}/_search: query, size, from, sort, _source, aggs... in the body. */
  async search(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const path = `${optSegment(p, "index")}/_search${queryString(p, SEARCH_QUERY)}`;
    return (await this.t.request("POST", path, without(p, "index," + SEARCH_QUERY), false, options ?? null)).json();
  }

  /** POST /{index}/_count */
  async count(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const keys = "routing,preference,ignore_unavailable,allow_no_indices,expand_wildcards,q,df,analyzer,default_operator,lenient,min_score,terminate_after";
    return (await this.t.request("POST", `${optSegment(p, "index")}/_count${queryString(p, keys)}`, without(p, "index," + keys), false, options ?? null)).json();
  }

  /** PUT /{index}/_doc/{id} (or POST /{index}/_doc without an id): the document is the body. */
  async index(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const keys = WRITE_QUERY + ",op_type,pipeline,require_alias";
    const id = optSegment(p, "id");
    const path = `/${segment(p, "index", "index")}/_doc${id}${queryString(p, keys)}`;
    const doc = field(p, "document");
    return (await this.t.request(id !== "" ? "PUT" : "POST", path, doc !== "" ? doc : "{}", false, options ?? null)).json();
  }

  /** GET /{index}/_doc/{id}: a missing document is an error (404), unless options.ignore has 404. */
  async get(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const keys = "_source,_source_includes,_source_excludes,routing,realtime,refresh,preference,stored_fields,version,version_type";
    const path = `/${segment(p, "index", "get")}/_doc/${segment(p, "id", "get")}${queryString(p, keys)}`;
    return (await this.t.request("GET", path, "", false, options ?? null)).json();
  }

  /** POST /{index}/_update/{id}: doc, doc_as_upsert, script, upsert in the body. */
  async update(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const keys = WRITE_QUERY + ",retry_on_conflict,require_alias,_source,_source_includes,_source_excludes,lang";
    const path = `/${segment(p, "index", "update")}/_update/${segment(p, "id", "update")}${queryString(p, keys)}`;
    return (await this.t.request("POST", path, without(p, "index,id," + keys), false, options ?? null)).json();
  }

  /** DELETE /{index}/_doc/{id} */
  async delete(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const path = `/${segment(p, "index", "delete")}/_doc/${segment(p, "id", "delete")}${queryString(p, WRITE_QUERY)}`;
    return (await this.t.request("DELETE", path, "", false, options ?? null)).json();
  }

  /** POST /[{index}/]_bulk: operations is the array of action and document lines, sent as NDJSON. */
  async bulk(params: object, options?: TransportRequestOptions): Promise<object> {
    const p = JSON.stringify(params);
    const keys = "refresh,routing,pipeline,timeout,wait_for_active_shards,require_alias,_source,_source_includes,_source_excludes";
    const ops = field(p, "operations");
    const body = ops !== "" ? ndjson(ops) : "";
    if (body === "") throw new Error("Missing required parameter: operations (bulk)");
    return (await this.t.request("POST", `${optSegment(p, "index")}/_bulk${queryString(p, keys)}`, body, true, options ?? null)).json();
  }

  /** HEAD /: true when the cluster answers. */
  async ping(options?: TransportRequestOptions): Promise<boolean> {
    try {
      const r = await this.t.request("HEAD", "/", "", false, options ?? null);
      return r.status === 200;
    } catch (e) {
      return false;
    }
  }

  /** GET / */
  async info(options?: TransportRequestOptions): Promise<object> {
    return (await this.t.request("GET", "/", "", false, options ?? null)).json();
  }

  /** Close the client: later calls fail. */
  async close(): Promise<void> {
    this.t.closed = true;
  }
}
