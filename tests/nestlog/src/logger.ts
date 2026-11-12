import { Logger } from "@nestjs/common";

// Nest's Logger on the console (no app.useLogger): object messages, a context, an error with text after the object
const logger = new Logger("Billing");
logger.log("plain text");
logger.warn({ event: "auth_fail", reason: "ip_missing", tries: 3 });
logger.error({ event: "insert_fail", id: "a1" }, "could not persist");
logger.error("only text");
logger.log({ nested: { list: [1, 2], empty: {}, none: [] }, ok: true });
Logger.warn({ event: "static" });
Logger.log("static text", "Other");
new Logger("").warn({ event: "no context" });
