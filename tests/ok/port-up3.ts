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

// an object literal where Record<string, unknown> | null goes (an optional details argument)
interface Sink {
  start(id: number, detail?: Record<string, unknown>): void;
}
class Collector implements Sink {
  start(id: number, detail?: Record<string, unknown>): void {
    console.log(id, JSON.stringify(detail ?? {}));
  }
}
interface Row {
  code: string;
  who: number;
  origin: string;
}
function begin(sink: Sink | null, r: Row): void {
  sink?.start(r.who, { code: r.code, who: r.who, origin: r.origin });
}
begin(new Collector(), { code: "A1", who: 3, origin: "C" });
const w = 3;
const d: Record<string, unknown> | null = { a: "x", b: w };
console.log(JSON.stringify(d));
