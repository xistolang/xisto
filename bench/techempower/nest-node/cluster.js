// One process per CPU, as the TFB's own NestJS app (frameworks/TypeScript/nest/src/main.ts) and Xisto's
// "2 processes on one port" mode: each worker loads the same compiled app (dist/main.js) and the primary only forks.
// Node shares the listening socket through node:cluster; Bun implements node:cluster with SO_REUSEPORT on Linux.
const cluster = require('node:cluster');
const os = require('node:os');

if (cluster.isPrimary) {
  const n = Number(process.env.WORKERS || os.availableParallelism());
  for (let i = 0; i < n; i++) cluster.fork();
  cluster.on('exit', () => process.exit(1));
} else {
  require('./dist/main.js');
}
