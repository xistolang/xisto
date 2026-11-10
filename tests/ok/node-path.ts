import * as path from "path";
import { join, dirname, basename, extname, resolve, normalize, relative, isAbsolute, sep } from "node:path";
import { performance } from "node:perf_hooks";
const t0 = performance.now();
const cases = ["/a/b/c.txt", "a/b/", "/", "", ".", "a//b/../c/./d.tar.gz", "/x/..", "../../a", "file", ".bashrc", "/a/b//"];
for (const c of cases) {
  console.log(JSON.stringify(c), normalize(c), dirname(c), basename(c), extname(c), isAbsolute(c));
}
console.log(join("a", "b", "../c"), join(), join("/a", "", "b/"), path.join("x", "y"));
console.log(basename("/a/b.ts", ".ts"), resolve("/a/b", "../c"), resolve("/a", "/b", "c"), relative("/a/b/c", "/a/d"), sep);
console.log(resolve("x") === join(process.cwd(), "x"), performance.now() >= t0);
