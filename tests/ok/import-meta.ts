// import.meta.url, import.meta.dirname and import.meta.filename: the module's source file (xisto: where it was built),
// with fileURLToPath and new URL("./x", import.meta.url) to name a file next to it (not read here: tests/cross runs
// the binaries on another machine, where the build's paths are not)
import { fileURLToPath } from "node:url";
import { dirname, join, basename } from "node:path";

import.meta.url.startsWith("file:///");
const here = fileURLToPath(import.meta.url);
console.log(import.meta.url.startsWith("file:///"), import.meta.url.endsWith("/tests/ok/import-meta.ts"));
console.log(here === import.meta.filename, dirname(here) === import.meta.dirname, basename(import.meta.filename));
console.log(import.meta.dirname.endsWith("/tests/ok"), import.meta.filename.startsWith("/"));
const data = new URL("./data dir/x1.json", import.meta.url);
console.log(data.protocol, data.href.endsWith("/tests/ok/data%20dir/x1.json"), fileURLToPath(data) === join(import.meta.dirname, "data dir/x1.json"));
console.log(new URL("../import-meta.ts", import.meta.url).pathname.endsWith("/tests/import-meta.ts"));
function where(): string {
  return import.meta.filename;
}
console.log(where() === here);
