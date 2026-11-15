// node:os: machine, endianness, devNull, uptime, loadavg, totalmem/freemem, userInfo, networkInterfaces (the loopback
// interface), constants (signals, errno, priority)
import os from "node:os";
import { constants, userInfo, networkInterfaces } from "node:os";
console.log(os.machine(), os.endianness(), os.devNull, typeof os.uptime(), os.uptime() > 0, os.loadavg().length, os.loadavg()[0] >= 0);
console.log(os.totalmem() > os.freemem(), os.freemem() > 0, Number.isInteger(os.totalmem()), os.totalmem());
console.log(JSON.stringify(userInfo()), os.userInfo().homedir.length > 0);
console.log(Object.keys(constants.signals).join(), constants.signals.SIGINT, constants.signals.SIGTERM, constants.signals.SIGUSR1);
console.log(Object.keys(os.constants.errno).join(), os.constants.errno.ENOENT, os.constants.errno.ENOTEMPTY, JSON.stringify(os.constants.priority));
const ni = networkInterfaces();
console.log(ni.lo0 ?? ni.lo);
console.log(Object.keys(ni).length > 0, os.release().length > 0, os.version().length > 0, os.type(), os.platform(), os.arch());
