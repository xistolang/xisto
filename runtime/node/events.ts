// xisto:events (node:events): EventEmitter, once, getEventListeners, setMaxListeners, errorMonitor.
// Listeners are functions of any parameters: a typed function given where (...args: any[]) => void goes is wrapped
// by the compiler in an adapter that keeps the function itself, so off(fn) and listeners() see the same function.
// Differences from Node (docs/docs/guides/node-compatibility.md):
//   - event names are strings; errorMonitor is a reserved string, not a symbol;
//   - a listener called with fewer arguments than it declares gets null for the missing ones (Node: undefined),
//     and a missing argument of a non-nullable type is a TypeError;
//   - rawListeners() gives the listeners themselves (no once wrappers); events.on() (async iteration) is missing.

/** A listener: any function; its arguments are what emit() is given after the event name. */
export type Listener = (...args: any[]) => void;

class Entry {
  fn: Listener;
  once: boolean;
  constructor(fn: Listener, once: boolean) {
    this.fn = fn;
    this.once = once;
  }
}

/** The name under which listeners see every 'error' before the 'error' listeners (Node: a symbol). */
export const errorMonitor: string = "events.errorMonitor";

function errorText(err: any): string {
  if (err instanceof Error) return (err as Error).message;
  return String(err);
}

/** Emits named events to the listeners registered for them, in the order they were added. */
export class EventEmitter {
  private _events: Map<string, Entry[]> = new Map<string, Entry[]>();
  private _maxListeners: int = -1;
  private _warned: Map<string, boolean> = new Map<string, boolean>();

  /** The maximum number of listeners per event before a warning, for emitters that do not set their own. */
  static defaultMaxListeners: int = 10;
  static readonly errorMonitor: string = errorMonitor;

  private add(event: string, fn: Listener, once: boolean, prepend: boolean): EventEmitter {
    if (this._events.has("newListener")) this.emit("newListener", event, fn);
    const found = this._events.get(event);
    const list: Entry[] = found !== null ? found : [];
    if (found === null) this._events.set(event, list);
    const entry = new Entry(fn, once);
    if (prepend) list.unshift(entry);
    else list.push(entry);
    const max = this.getMaxListeners();
    if (max > 0 && list.length > max && !this._warned.has(event)) {
      this._warned.set(event, true);
      console.error(
        `(node:${process.pid}) MaxListenersExceededWarning: Possible EventEmitter memory leak detected. ${list.length} ${event} listeners added to [${this.constructor.name}]. MaxListeners is ${max}. Use emitter.setMaxListeners() to increase limit\n(Use \`node --trace-warnings ...\` to show where the warning was created)`,
      );
    }
    return this;
  }

  /** Adds a listener for the event, called on each emit. */
  on(event: string, listener: Listener): EventEmitter {
    return this.add(event, listener, false, false);
  }
  /** The same as on(). */
  addListener(event: string, listener: Listener): EventEmitter {
    return this.add(event, listener, false, false);
  }
  /** Adds a listener called at the next emit only. */
  once(event: string, listener: Listener): EventEmitter {
    return this.add(event, listener, true, false);
  }
  /** Adds a listener before the others. */
  prependListener(event: string, listener: Listener): EventEmitter {
    return this.add(event, listener, false, true);
  }
  /** Adds a once listener before the others. */
  prependOnceListener(event: string, listener: Listener): EventEmitter {
    return this.add(event, listener, true, true);
  }

  /** Removes the most recently added registration of the listener for the event. */
  removeListener(event: string, listener: Listener): EventEmitter {
    const list = this._events.get(event);
    if (list === null) return this;
    for (let i = list.length - 1; i >= 0; i--) {
      if (list[i].fn === listener) {
        list.splice(i, 1);
        if (list.length === 0) this._events.delete(event);
        if (this._events.has("removeListener")) this.emit("removeListener", event, listener);
        this._listenerRemoved(event, listener);
        break;
      }
    }
    return this;
  }
  /** What a subclass does once a listener is gone: after off()/removeListener(), and when a once() listener is
   * removed before its call (node:stream's Readable watches 'readable' and 'data' so). */
  protected _listenerRemoved(event: string, listener: Listener): void {}
  /** The same as removeListener(). */
  off(event: string, listener: Listener): EventEmitter {
    return this.removeListener(event, listener);
  }

  /** Removes every listener of the event, or of all events when none is named. */
  removeAllListeners(event: string | null = null): EventEmitter {
    if (event !== null) {
      const list = this._events.get(event);
      if (list === null) return this;
      if (event !== "removeListener" && this._events.has("removeListener")) {
        for (let i = list.length - 1; i >= 0; i--) this.removeListener(event, list[i].fn);
      } else {
        this._events.delete(event);
      }
      return this;
    }
    if (this._events.has("removeListener")) {
      for (const name of this.eventNames()) if (name !== "removeListener") this.removeAllListeners(name);
      this.removeAllListeners("removeListener");
    }
    this._events = new Map<string, Entry[]>();
    return this;
  }

  /** Calls each listener of the event with the arguments, in order; true when there was at least one. */
  emit(event: string, ...args: any[]): boolean {
    if (event === "error" && this._events.has(errorMonitor)) this.emit(errorMonitor, ...args);
    const list = this._events.get(event);
    if (list === null || list.length === 0) {
      if (event === "error") {
        const err: any = args.length > 0 ? args[0] : null;
        if (err instanceof Error) throw err as Error;
        throw new Error(`Unhandled error. (${err === null ? "undefined" : errorText(err)})`);
      }
      return false;
    }
    const snapshot = list.slice();
    for (const e of snapshot) {
      if (e.once) {
        const cur = this._events.get(event);
        if (cur !== null) {
          for (let i = 0; i < cur.length; i++) {
            if (cur[i] === e) {
              cur.splice(i, 1);
              break;
            }
          }
          if (cur.length === 0) this._events.delete(event);
        }
        // (Node's once wrapper removes itself with removeListener before the call: 'removeListener' is emitted)
        if (this._events.has("removeListener")) this.emit("removeListener", event, e.fn);
        this._listenerRemoved(event, e.fn);
      }
      e.fn(...args);
    }
    return true;
  }

  /** The number of listeners of the event (of that listener only, when given). */
  listenerCount(event: string, listener: Listener | null = null): int {
    const list = this._events.get(event);
    if (list === null) return 0;
    if (listener === null) return list.length;
    let n: int = 0;
    for (const e of list) if (e.fn === listener) n++;
    return n;
  }
  /** The listeners of the event, in the order they are called. */
  listeners(event: string): Listener[] {
    const out: Listener[] = [];
    const list = this._events.get(event);
    if (list !== null) for (const e of list) out.push(e.fn);
    return out;
  }
  /** The same as listeners() (Node gives the once wrappers here). */
  rawListeners(event: string): Listener[] {
    return this.listeners(event);
  }
  /** The names of the events that have listeners, in the order they first got one. */
  eventNames(): string[] {
    const out: string[] = [];
    for (const k of this._events.keys()) out.push(k);
    return out;
  }
  /** Sets the number of listeners per event above which a warning is printed (0: no limit). */
  setMaxListeners(n: int): EventEmitter {
    if (n < 0) throw new RangeError(`The value of "n" is out of range. It must be a non-negative number. Received ${n}`);
    this._maxListeners = n;
    return this;
  }
  /** The limit of setMaxListeners(), or the default. */
  getMaxListeners(): int {
    return this._maxListeners < 0 ? EventEmitter.defaultMaxListeners : this._maxListeners;
  }
}

export default EventEmitter;

/** A promise of the arguments of the next emit of the event; rejected by an 'error' emitted first. */
export function once(emitter: EventEmitter, event: string): Promise<any[]> {
  return new Promise<any[]>((resolve, reject) => {
    let onEvent: Listener | null = null;
    const onError = (...errs: any[]): void => {
      if (onEvent !== null) emitter.removeListener(event, onEvent);
      reject(errs[0] as Error);
    };
    onEvent = (...args: any[]): void => {
      if (event !== "error") emitter.removeListener("error", onError);
      resolve(args);
    };
    emitter.once(event, onEvent);
    if (event !== "error") emitter.once("error", onError);
  });
}

/** The listeners of an emitter's event. */
export function getEventListeners(emitter: EventEmitter, event: string): Listener[] {
  return emitter.listeners(event);
}

/** Sets the listener limit of the emitters given, or the default for all when none is. */
export function setMaxListeners(n: int, ...emitters: EventEmitter[]): void {
  if (emitters.length === 0) {
    EventEmitter.defaultMaxListeners = n;
    return;
  }
  for (const e of emitters) e.setMaxListeners(n);
}

/** The listener limit of an emitter. */
export function getMaxListeners(emitter: EventEmitter): int {
  return emitter.getMaxListeners();
}

/** The number of listeners of an emitter's event (deprecated in Node: emitter.listenerCount(event)). */
export function listenerCount(emitter: EventEmitter, event: string): int {
  return emitter.listenerCount(event);
}
