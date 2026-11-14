// an object of a class that extends Error, read back as Error from an unknown (a variable, and an EventEmitter
// listener typed (e: Error)): the same error, instanceof its class
import { EventEmitter } from "node:events";

class HttpError extends Error {
  status: number;
  constructor(message: string, status: number) {
    super(message);
    this.name = "HttpError";
    this.status = status;
  }
}
const u: unknown = new HttpError("not found", 404);
const e = u as Error;
console.log(e.message, e.name, e instanceof HttpError, String(e));
const em = new EventEmitter();
const onError = (err: Error): void => console.log("listener:", err.message, err instanceof HttpError);
em.on("error", onError);
em.on("error", (err: Error) => console.log("inline:", err.name));
em.emit("error", new HttpError("gone", 410));
