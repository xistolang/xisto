// XB-117: import { Logger as PinoLogger } next to import { Logger } of another module: each name is its own class
import { Logger as PinoLogger } from "./lib/alias-pino.ts";
import { Logger } from "./lib/alias-nest.ts";
const a = new Logger("Bootstrap");
const b: PinoLogger = new PinoLogger();
console.log(a.hello(), b.hello());
