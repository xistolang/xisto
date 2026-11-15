// import.meta.url, import.meta.dirname and import.meta.filename: the module's source file (xisto: where it was built),
// with fileURLToPath and new URL("./x", import.meta.url) to find a file next to it
import { fileURLToPath } from "node:url";
import { dirname, join, basename } from "node:path";
import { readFileSync } from "node:fs";

import.meta.url.startsWith("file:///");
const here = fileURLToPath(import.meta.url);
console.log(import.meta.url.startsWith("file:///"), import.meta.url.endsWith("/tests/ok/import-meta.ts"));
console.log(here === import.meta.filename, dirname(here) === import.meta.dirname, basename(import.meta.filename));
console.log(import.meta.dirname.endsWith("/tests/ok"), import.meta.filename.startsWith("/"));
const data = new URL("./import-meta.ts", import.meta.url);
console.log(data.protocol, readFileSync(fileURLToPath(data), "utf8").split("\n")[0].slice(0, 16));
console.log(readFileSync(join(import.meta.dirname, "import-meta.ts"), "utf8").length > 100);
function where(): string {
  return import.meta.filename;
}
console.log(where() === here);
