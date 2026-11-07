/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

binary-trees, xisto #3: a pool of workers (node:worker_threads), one per core, and the work in pieces: each
depth's trees are split into chunks, and a worker that hands in a chunk's checksum gets the next chunk. Cores
of different speeds (Apple's performance and efficiency cores) then all stay busy to the end.
 */

import { availableParallelism } from "node:os";
import { Worker, isMainThread, parentPort, workerData } from "node:worker_threads";

class TreeNode {
  left: TreeNode | null;
  right: TreeNode | null;
  constructor(left: TreeNode | null, right: TreeNode | null) {
    this.left = left;
    this.right = right;
  }
}

function checksum(node: TreeNode): int {
  const l = node.left;
  if (l === null) return 1;
  return 1 + checksum(l) + checksum(node.right!);
}

function createTree(depth: int): TreeNode {
  return depth > 0 ? new TreeNode(createTree(depth - 1), createTree(depth - 1)) : new TreeNode(null, null);
}

class Chunk {
  task: int = 0; // which line of the output
  depth: int = 0;
  trees: int = 0;
}

class Done {
  task: int = 0;
  check: int = 0;
}

function mainThread(): void {
  const maxDepth = Math.max(6, Math.trunc(Number(process.argv[2])));
  const depths: int[] = [maxDepth + 1];
  const iterations: int[] = [1];
  for (let depth = 4; depth <= maxDepth; depth += 2) {
    depths.push(depth);
    iterations.push(1 << (maxDepth - depth + 4));
  }
  // the pieces, the biggest first (the last ones handed out are small: the cores finish together)
  const chunks: Chunk[] = [];
  const pieces: int = 16;
  for (let t = 0; t < depths.length; t++) {
    const per: int = Math.max(1, Math.trunc(iterations[t] / pieces));
    for (let done: int = 0; done < iterations[t]; done += per) {
      const c = new Chunk();
      c.task = t;
      c.depth = depths[t];
      c.trees = Math.min(per, iterations[t] - done);
      chunks.push(c);
    }
  }
  chunks.sort((a: Chunk, b: Chunk) => b.trees * (1 << b.depth) - a.trees * (1 << a.depth));
  const checks: int[] = new Array<int>(depths.length).fill(0);
  let next = 0;
  let left = chunks.length;
  const longLivedTree = createTree(maxDepth);
  const nworkers = Math.min(availableParallelism(), chunks.length);
  const workers: Worker[] = [];
  for (let k = 0; k < nworkers; k++) {
    const w = new Worker(__filename, { workerData: "" });
    workers.push(w);
    w.on("message", (m: string) => {
      const d: Done = JSON.parse(m);
      checks[d.task] += d.check;
      left--;
      if (next < chunks.length) {
        w.postMessage(JSON.stringify(chunks[next]));
        next++;
      } else w.terminate();
      if (left === 0) {
        for (let t = 0; t < depths.length; t++) {
          if (t === 0) process.stdout.write(`stretch tree of depth ${depths[t]}\t check: ${checks[t]}\n`);
          else process.stdout.write(`${iterations[t]}\t trees of depth ${depths[t]}\t check: ${checks[t]}\n`);
        }
        process.stdout.write(`long lived tree of depth ${maxDepth}\t check: ${checksum(longLivedTree)}\n`);
      }
    });
    w.postMessage(JSON.stringify(chunks[next]));
    next++;
  }
}

function work(depth: int, trees: int): int {
  let check: int = 0;
  for (let i = 0; i < trees; i++) check += checksum(createTree(depth));
  return check;
}

function workerThread(): void {
  parentPort.on("message", (m: string) => {
    const c: Chunk = JSON.parse(m);
    const check = work(c.depth, c.trees);
    const d = new Done();
    d.task = c.task;
    d.check = check;
    parentPort.postMessage(JSON.stringify(d));
  });
}

if (isMainThread) mainThread(); else workerThread();
