// import fs from "node:fs" / import path from "path" (esModuleInterop): the default import of a Node module is the module
import path from "node:path";
import fs from "fs";
import os from "node:os";
import crypto from "node:crypto";
import EventEmitter from "node:events";

console.log(path.join("a", "b", "../c"), path.extname("x.ts"));
console.log(fs.existsSync("/"), os.EOL === "\n");
console.log(crypto.createHash("sha256").update("xisto").digest("hex"));
const e = new EventEmitter();
e.on("x", (v: number) => console.log("x", v));
e.emit("x", 1);
