// node:v8: getHeapStatistics' fields (a limit V8 would set), serialize's bytes
// build: --node-compat
import { getHeapStatistics, serialize } from 'node:v8';
const h = getHeapStatistics();
console.log(h.heap_size_limit > 1e8, typeof h.used_heap_size, Object.keys(h).length);
console.log(serialize({ a: 1, b: 'xy' }).byteLength > 0);
