// process.stdin piped through a Transform to process.stdout (a Writable here); the copy ends when stdin does
import { Transform } from "node:stream";

const upper = new Transform({
  transform: (chunk: any, encoding: string, callback: (error?: Error | null, data?: any) => void): void => {
    callback(null, (chunk as Buffer).toString().toUpperCase());
  },
});
process.stdin.pipe(upper).pipe(process.stdout);
upper.on("end", () => console.log("[piped]"));
