// appa-up-3: forms the big build reached in C
interface TreeItem {
  id: number;
  label: string;
  children?: TreeItem[];
}

function buildTree(): TreeItem[] {
  const root: TreeItem = { id: 1, label: "root", children: [] };
  const leaf: TreeItem = { id: 2, label: "leaf" };
  root.children?.push(leaf);
  return [root];
}

const tree = buildTree();
const text = JSON.stringify(tree);
console.log(text);
const back = JSON.parse(text) as TreeItem[];
console.log(back[0].children?.length ?? 0, back[0].children?.[0].label);

// join over values of mixed kinds, as String(x) each, null as ""
const parts: unknown[] = ["a", 1, true, null, 2.5];
console.log(parts.join("-"));
const key = [7, "x", null].join("|");
console.log(key);
