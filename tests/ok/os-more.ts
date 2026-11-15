// node:os: machine, endianness, devNull, uptime, loadavg, totalmem/freemem, userInfo, networkInterfaces (the loopback
// interface), constants (signals, errno, priority)
import os from "node:os";
import { constants, userInfo, networkInterfaces } from "node:os";
console.log(["arm64", "aarch64", "x86_64"].includes(os.machine()), os.endianness(), os.devNull, typeof os.uptime(), os.uptime() > 0, os.loadavg().length, os.loadavg()[0] >= 0);
console.log(os.totalmem() > os.freemem(), os.freemem() > 0, Number.isInteger(os.totalmem()), os.totalmem() > 1e8);
const u = userInfo();
console.log(Object.keys(u).join(), u.uid === process.getuid(), u.username.length > 0, os.userInfo().homedir.length > 0, typeof u.shell);
console.log(constants.signals.SIGHUP, constants.signals.SIGINT, constants.signals.SIGKILL, constants.signals.SIGTERM, Object.keys(constants.signals).includes("SIGUSR1"), Object.keys(constants.signals).length > 25);
console.log(os.constants.errno.ENOENT, os.constants.errno.EEXIST, os.constants.errno.EACCES, Object.keys(os.constants.errno).includes("ENOTEMPTY"), JSON.stringify(os.constants.priority));
const ni = networkInterfaces();
for (const list of Object.values(ni)) for (const a of list ?? []) if (a.internal && a.family === "IPv4") console.log(a);
console.log(Object.keys(ni).length > 0, os.release().length > 0, os.version().length > 0, os.type() === (os.platform() === "darwin" ? "Darwin" : "Linux"));
