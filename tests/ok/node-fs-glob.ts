// fs.globSync, fs.glob (callback) and fs.promises.glob: Node's patterns, options and order of results
import * as fs from "node:fs";
import * as fsp from "node:fs/promises";
import * as os from "node:os";
import * as path from "node:path";

const root = fs.mkdtempSync(path.join(os.tmpdir(), "xisto-glob-"));
for (const d of ["src/a/b", "src/c", ".hid", "lib"]) fs.mkdirSync(path.join(root, d), { recursive: true });
for (const f of ["x.ts", "y.js", "src/z.ts", "src/a/m.ts", "src/a/b/n.ts", "src/c/o.ts", "src/c/p.md", ".hid/q.ts", ".dot.ts", "README.md", "lib/k1.js", "lib/k2.js", "lib/k10.js"]) {
  fs.writeFileSync(path.join(root, f), "");
}
const o = { cwd: root };
const patterns = ["*.ts", "**/*.ts", "src/*", "src/**", "**", "src/{a,c}/*.ts", "src/?/*", "*", "src/a/**/*.ts", "**/*.md", "src/[ac]/*",
  "./*.ts", "src/../x.ts", "src/a/../c/*", "lib/k{1..2}.js", "lib/k[!1]*.js", "**/", "src/**/", ".*", "**/.*", ".hid/*", "nope/*", "src/*/*/*"];
for (const p of patterns) console.log(JSON.stringify(p), JSON.stringify(fs.globSync(p, o)));
console.log(JSON.stringify(fs.globSync(["*.js", "*.md", "*.js"], o)));
console.log(JSON.stringify(fs.globSync("**/*.ts", { cwd: root, exclude: ["src/a/**"] })));
console.log(JSON.stringify(fs.globSync("**/*.ts", { cwd: root, exclude: (p: string) => p.endsWith("z.ts") })));
console.log(JSON.stringify(fs.globSync("*.ts", { cwd: path.join(root, "src") })));
console.log(JSON.stringify(fs.globSync(root + "/src/c/*").map((p) => p.slice(root.length))));
console.log(JSON.stringify(fs.globSync("src/*", { cwd: root, withFileTypes: true }).map((d) => [d.name, d.isDirectory(), d.parentPath === path.join(root, "src")])));

fs.glob("lib/*.js", o, (err: Error | null, matches: string[]) => {
  console.log("callback", err, JSON.stringify(matches));
  void (async (): Promise<void> => {
    const seen: string[] = [];
    for await (const f of fsp.glob("src/**/*.ts", o)) seen.push(f);
    console.log("promises", JSON.stringify(seen));
    fs.rmSync(root, { recursive: true });
  })();
});
