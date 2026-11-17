// node:path/posix and node:path/win32 as modules; path.win32.resolve and relative
import * as posix from "node:path/posix";
import * as win from "node:path/win32";
import pp from "node:path/posix";
import { join, sep } from "node:path/win32";
import * as path from "node:path";

console.log(posix.sep, posix.delimiter, posix.join("/a", "b", "../c"), posix.basename("/x/y.ts", ".ts"));
console.log(posix.normalize("/a//b/./c/.."), posix.relative("/a/b/c", "/a/d"), posix.isAbsolute("x"));
console.log(posix.resolve("/root", "x", "..", "y"), pp.extname("a.tar.gz"), pp.dirname("/a/b/c"));
console.log(JSON.stringify(posix.parse("/home/u/file.txt")), posix.format({ dir: "/d", base: "f.js" }));
console.log(posix.posix.join("a", "b"), posix.win32.join("a", "b"));

console.log(win.sep, win.delimiter, sep, join("C:\\a", "b", "..\\c"));
console.log(win.normalize("C:/x//y/../z"), win.isAbsolute("C:\\x"), win.isAbsolute("x"));
console.log(win.dirname("C:\\a\\b"), win.basename("C:\\a\\b.txt", ".txt"), win.extname("x.md"));
console.log(JSON.stringify(win.parse("C:\\dir\\f.txt")), win.format({ root: "C:\\", name: "f", ext: ".js" }));
console.log(win.toNamespacedPath("C:\\a\\b"), win.toNamespacedPath("\\\\srv\\share\\x"));

const abs = [
  ["C:\\a", "b"], ["C:\\a", "D:\\b", "c"], ["\\\\srv\\share\\x", "..\\y"], ["C:\\", "..", "x"], ["c:/a/b", "./c/"],
  ["C:\\a\\", "\\b"], ["\\\\.\\PHYSICALDRIVE0", "x"], ["D:\\x", "D:y"],
];
for (const a of abs) console.log(win.resolve(...a), path.win32.resolve(...a));
const rel = [
  ["C:\\a\\b\\c", "C:\\a\\d"], ["C:\\a", "C:\\a\\b\\c"], ["C:\\a\\b", "C:\\a"], ["C:\\", "C:\\x"], ["C:\\x\\y", "C:\\"],
  ["C:\\A\\b", "c:\\a\\B\\c"], ["C:\\a", "D:\\a"], ["\\\\s\\sh\\a", "\\\\s\\sh\\b"], ["C:\\foo", "C:\\foobar"],
  ["C:\\Ä\\b", "C:\\ä\\c"], ["C:\\x", "C:\\x"],
];
for (const r of rel) console.log(JSON.stringify(win.relative(r[0], r[1])), JSON.stringify(path.win32.relative(r[0], r[1])));
// relative paths resolve against the current directory: only their shape is stable across machines
console.log(win.resolve("x").endsWith("\\x"), win.resolve().includes("/"), win.relative("a", "a\\b"));
