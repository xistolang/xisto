// xisto:diagnostics_channel (node:diagnostics_channel): named channels that modules publish to and observers (APM
// agents) subscribe to, as Node's. Published here, with Node's names and message shapes:
//   http.server.request.start / http.server.response.finish   { request, response, server }   (node:http servers)
//   http.client.request.start / http.client.response.finish / http.client.request.error   { request, response, error }
//   undici:request:create / undici:request:headers / undici:request:error   { request, response, error }   (fetch)
// For fetch, request is a FetchRequest: origin, method, path, headers (name, value, name, value...) and addHeader(),
// which adds a header to the request before it is sent (as Node's undici request does).
// Differences from Node: the class is DiagnosticsChannel (Channel is xisto's channel between tasks); messages are any
// (objects of the classes below); no TracingChannel, no channel(Symbol).

/** A subscriber: called with each message and the channel's name. */
export type ChannelListener = (message: any, name: string) => void;

/** A named channel. */
export class DiagnosticsChannel {
  name: string;
  /** true while someone listens (publishers test it first: publishing to nobody costs nothing) */
  hasSubscribers: boolean = false;
  private subs: ChannelListener[] = [];
  constructor(name: string) {
    this.name = name;
  }
  subscribe(fn: ChannelListener): void {
    this.subs.push(fn);
    this.hasSubscribers = true;
    onSubscribe(this.name);
  }
  /** true when fn was subscribed */
  unsubscribe(fn: ChannelListener): boolean {
    for (let i = 0; i < this.subs.length; i++) {
      if (this.subs[i] === fn) {
        this.subs.splice(i, 1);
        this.hasSubscribers = this.subs.length > 0;
        return true;
      }
    }
    return false;
  }
  publish(message: any): void {
    for (const fn of this.subs.slice()) fn(message, this.name);
  }
}

const channels: Map<string, DiagnosticsChannel> = new Map<string, DiagnosticsChannel>();

/** The channel of that name (made once). */
export function channel(name: string): DiagnosticsChannel {
  const c = channels.get(name);
  if (c !== null) return c;
  const n = new DiagnosticsChannel(name);
  channels.set(name, n);
  return n;
}
export function subscribe(name: string, fn: ChannelListener): void {
  channel(name).subscribe(fn);
}
export function unsubscribe(name: string, fn: ChannelListener): boolean {
  return channel(name).unsubscribe(fn);
}
export function hasSubscribers(name: string): boolean {
  const c = channels.get(name);
  return c !== null && c.hasSubscribers;
}

// ---- fetch (undici:request:*) ----

/** A fetch request as undici's channels give it. */
export class FetchRequest {
  origin: string;
  method: string;
  path: string;
  /** name, value, name, value... (the ones addHeader() added) */
  headers: string[] = [];
  /** for the subscribers' own use (an APM agent's span) */
  context: any = null;
  constructor(origin: string, method: string, path: string) {
    this.origin = origin;
    this.method = method;
    this.path = path;
  }
  addHeader(name: string, value: string): FetchRequest {
    this.headers.push(name);
    this.headers.push(value);
    return this;
  }
}
/** undici:request:headers' response. */
export class FetchResponse {
  statusCode: int;
  constructor(statusCode: int) {
    this.statusCode = statusCode;
  }
}
/** The message of the undici:request:* channels. */
export class FetchMessage {
  request: FetchRequest;
  response: FetchResponse | null = null;
  error: Error | null = null;
  constructor(request: FetchRequest) {
    this.request = request;
  }
}

declare function fetch_hooks(start: (url: string, method: string) => string, end: (id: string, status: int, error: string) => void): void;

const inflight: Map<string, FetchRequest> = new Map<string, FetchRequest>();
let nextId: int = 1;
let hooked = false;
let skipNext = false;

/** (node:http's client is made over fetch: its own call is not a fetch for the undici channels, as in Node) */
export function _skipNextFetch(): void {
  skipNext = hooked;
}

function fetchStart(url: string, method: string): string {
  if (skipNext) {
    skipNext = false; // (once: the call that set it is the one starting now, before it waits)
    return "0";
  }
  const id = String(nextId);
  nextId++;
  const u = new URL(url);
  const req = new FetchRequest(u.origin, method, u.pathname + u.search);
  inflight.set(id, req);
  channel("undici:request:create").publish(new FetchMessage(req));
  let out = id;
  for (let i = 0; i + 1 < req.headers.length; i += 2) out += `\n${req.headers[i]}\n${req.headers[i + 1]}`;
  return out;
}
function fetchEnd(id: string, status: int, error: string): void {
  const req = inflight.get(id);
  if (req === null) return;
  inflight.delete(id);
  const m = new FetchMessage(req);
  if (error.length > 0) {
    m.error = new Error(error);
    channel("undici:request:error").publish(m);
  } else {
    m.response = new FetchResponse(status);
    channel("undici:request:headers").publish(m);
  }
}
function onSubscribe(name: string): void {
  if (!hooked && name.startsWith("undici:request:")) {
    hooked = true;
    fetch_hooks(fetchStart, fetchEnd);
  }
}
