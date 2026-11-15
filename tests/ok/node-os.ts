// node:os and os: hostname, platform, type, arch, tmpdir, homedir, EOL, release and version, as Node gives them
import { hostname, platform, type, arch, tmpdir, homedir, EOL, availableParallelism, release, version } from "node:os";
import { hostname as host2 } from "os";

console.log(hostname().length > 0, hostname() === host2());
console.log(platform(), type(), arch());
console.log(tmpdir().length > 0, !tmpdir().endsWith("/") || tmpdir() === "/");
console.log(homedir().length > 0, JSON.stringify(EOL), availableParallelism() > 0);
console.log(/^[0-9]+\.[0-9]+/.test(release()), version().length > 0, version() !== release());
