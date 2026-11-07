/* The Computer Language Benchmarks Game
https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

binary-trees, xisto #2: the multi-thread Node program (binarytrees.node, by François Pirsch, after Léo
Sarrazin and Andrey Filatkin) with types: each depth is a task for a worker (node:worker_threads), its
result comes back as text, and the main thread prints them in order.
 */

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

class Task {
  iterations: int = 0;
  depth: int = 0;
}

function mainThread(): void {
  const maxDepth = Math.max(6, Math.trunc(Number(process.argv[2])));
  const stretchDepth = maxDepth + 1;
  const tasks: Task[] = [];
  const first = new Task();
  first.iterations = 1;
  first.depth = stretchDepth;
  tasks.push(first);
  const longLivedTree = createTree(maxDepth);
  for (let depth = 4; depth <= maxDepth; depth += 2) {
    const t = new Task();
    t.iterations = 1 << (maxDepth - depth + 4);
    t.depth = depth;
    tasks.push(t);
  }
  const results: string[] = new Array<string>(tasks.length).fill("");
  let remaining = tasks.length;
  for (let i = 0; i < tasks.length; i++) {
    new Worker(__filename, { workerData: JSON.stringify(tasks[i]) }).on("message", (message: string) => {
      results[i] = message;
      remaining--;
      if (remaining === 0) {
        for (const r of results) process.stdout.write(r);
        process.stdout.write(`long lived tree of depth ${maxDepth}\t check: ${checksum(longLivedTree)}\n`);
      }
    });
  }
}

function workerThread(): void {
  const t: Task = JSON.parse(workerData);
  let check: int = 0;
  for (let i = 0; i < t.iterations; i++) check += checksum(createTree(t.depth));
  parentPort.postMessage(
    t.iterations === 1
      ? `stretch tree of depth ${t.depth}\t check: ${check}\n`
      : `${t.iterations}\t trees of depth ${t.depth}\t check: ${check}\n`
  );
}

if (isMainThread) mainThread(); else workerThread();
