// node:url: pathToFileURL (made absolute, percent-encoded as Node does: spaces, %, #, ?, \, non-ASCII) and
// fileURLToPath (decoded; its errors: another scheme, a host, an encoded /)
import { pathToFileURL, fileURLToPath } from "node:url";
console.log(pathToFileURL("rel/a").href === "file://" + process.cwd() + "/rel/a", fileURLToPath(pathToFileURL("rel/a")) === process.cwd() + "/rel/a");
for (const p of ["/a b/c#d?e%f", "/tmp/ü x", "/x/\\y", "/a\nb\tc\r", "/", "/x/", "/[a]{b}^`|~!$&'()*+,;=:@", "/\"<>"]) { const u = pathToFileURL(p); console.log("p", JSON.stringify(p), u.href, u.pathname, fileURLToPath(u) === (p.startsWith("/") ? p : process.cwd() + "/" + p)); }
for (const u of ["file:///a%20b/c", "file://localhost/x", "file:///a%2Fb", "http://x/y", "file://host/x", "file:///%E2%9C%93"]) { try { console.log(fileURLToPath(u)); } catch (e: any) { console.log(e.name, e.code, e.message); } }
console.log(fileURLToPath(new URL("file:///q/r%23s")));
