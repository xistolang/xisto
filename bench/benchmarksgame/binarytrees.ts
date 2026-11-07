/* The Computer Language Benchmarks Game
   https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

   contributed by Léo Sarrazin
   multi thread by Andrey Filatkin
   sequential by Isaac Gouy
   (xisto: the same program with types; node/binarytrees-node-7.js is the original)
*/

function mainThread(): void {
  const maxDepth = Math.max(6, Math.trunc(parseInt(process.argv[2])));

  const stretchDepth = maxDepth + 1;
  const check = itemCheck(bottomUpTree(stretchDepth));
  console.log(`stretch tree of depth ${stretchDepth}\t check: ${check}`);

  const longLivedTree = bottomUpTree(maxDepth);

  for (let depth = 4; depth <= maxDepth; depth += 2) {
    const iterations = 1 << maxDepth - depth + 4;
    work(iterations, depth);
  }

  console.log(`long lived tree of depth ${maxDepth}\t check: ${itemCheck(longLivedTree)}`);
}

function work(iterations: int, depth: int): void {
  let check = 0;
  for (let i = 0; i < iterations; i++) {
    check += itemCheck(bottomUpTree(depth));
  }
  console.log(`${iterations}\t trees of depth ${depth}\t check: ${check}`);
}

class TreeNode {
  left: TreeNode | null;
  right: TreeNode | null;
  constructor(left: TreeNode | null, right: TreeNode | null) {
    this.left = left;
    this.right = right;
  }
}

function itemCheck(node: TreeNode): number {
  if (node.left === null) {
    return 1;
  }
  return 1 + itemCheck(node.left) + itemCheck(node.right!);
}

function bottomUpTree(depth: int): TreeNode {
  return depth > 0
    ? new TreeNode(bottomUpTree(depth - 1), bottomUpTree(depth - 1))
    : new TreeNode(null, null);
}

mainThread();
