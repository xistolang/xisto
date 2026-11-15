// node:path: parse/format, path.posix and path.win32 (normalize, join, isAbsolute, dirname, basename, extname, parse,
// format, toNamespacedPath of absolute paths), toNamespacedPath
import path from "node:path";
import { posix, win32, parse, format } from "node:path";
const ps = ["", "/", "/home/user/dir/file.txt", "a/b/", ".bashrc", "file.", "..", "a/b/../c.tar.gz", "//x//y", "./a", "/a.b/c", "a..b", "...", "x/.y.z"];
for (const p of ps) console.log(JSON.stringify(p), JSON.stringify(path.parse(p)), path.format(path.parse(p)));
console.log(format({ dir: "/x", base: "y.z" }), format({ root: "/", name: "n", ext: "e" }), format({ name: "n", ext: ".e" }), format({ root: "/r/", dir: "/r/" , base: "b"}), format({}));
console.log(posix.sep, posix.delimiter, posix.join("a", "..", "b"), posix.basename("/x/y.js", ".js"), path.posix.dirname("/a/b"), posix.parse("/q/w.e").name, path.toNamespacedPath("/x"));
const ws = ["", "C:", "C:\\", "C:\\a\\b.txt", "C:a\\b", "\\\\server\\share", "\\\\server\\share\\", "\\\\server\\share\\dir\\f.e", "/a/b", "a\\..\\..\\b", "C:/x/./y/../z", "\\", "file.", "..\\x", "c:\\x\\", "\\\\?\\C:\\x", "x"];
for (const p of ws) console.log(JSON.stringify(p), win32.normalize(p), win32.isAbsolute(p), win32.dirname(p), win32.basename(p), win32.extname(p), JSON.stringify(win32.parse(p)), win32.isAbsolute(p) ? win32.toNamespacedPath(p) : "-");
console.log(win32.join("C:\\a", "b", "..", "c"), win32.join("\\\\srv", "share", "x"), win32.join("a/", "/b"), win32.join(), win32.join("", ""), win32.join("//a", "b"), path.win32.sep, win32.delimiter, win32.basename("C:\\x\\y.txt", ".txt"), win32.format({ root: "C:\\", name: "f", ext: "txt" }), win32.format({ dir: "C:\\d", base: "b" }));
