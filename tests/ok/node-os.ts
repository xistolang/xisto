// node:os and os: hostname, platform, type, arch, tmpdir, homedir and EOL, as Node gives them
import { hostname, platform, type, arch, tmpdir, homedir, EOL, availableParallelism } from "node:os";
import { hostname as host2 } from "os";

console.log(hostname().length > 0, hostname() === host2());
console.log(platform(), type(), arch());
console.log(tmpdir().length > 0, !tmpdir().endsWith("/") || tmpdir() === "/");
console.log(homedir().length > 0, JSON.stringify(EOL), availableParallelism() > 0);
