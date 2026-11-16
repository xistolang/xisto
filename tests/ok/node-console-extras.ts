// console.table (arrays of objects, of primitives, of arrays, objects of objects, the columns filter), console.dir
// with a depth, console.count / countReset, console.group / groupCollapsed / groupEnd (console.log & co indented
// inside, multi-line text too), console.debug: as Node prints them (stdout only: the oracle merges both streams)
class Item {
  constructor(name: string, price: number, tags: string[]) {
    this.name = name;
    this.price = price;
    this.tags = tags;
  }
  name: string;
  price: number;
  tags: string[];
}
const items = [new Item("pen", 1.5, ["office"]), new Item("日本茶", 12, ["food", "tea", "jp", "green"])];
console.table(items);
console.table(items, ["price"]);
console.table([{ a: 1, b: "x" }, { a: 2, b: "yy" }]);
const mixed: any[] = [1, "two", { x: 3 }, null, true];
console.table(mixed);
console.table({ r1: { a: 1 }, r2: { b: 2 } });
console.table([[1, 2], [3, 4, 5]]);
console.table([{ deep: { a: 1, b: 2, c: 3 }, small: { a: 1 } }]);
console.table("not tabular");
console.table(42);
console.dir({ a: { b: { c: { d: 1 } } } }, { depth: 0 });
console.dir({ a: { b: { c: { d: 1 } } } }, { depth: null });
console.dir({ a: { b: { c: { d: 1 } } } });
console.dir("quoted");
console.count();
console.count();
console.count("apples");
console.countReset();
console.count();
console.count("apples");
console.group("Group 1");
console.log("inside");
console.log("two\nlines");
console.group();
console.info({ nested: true });
console.debug([1, 2, 3]);
console.table([{ k: 1 }]);
console.count("apples");
console.dir({ x: 1 });
console.groupEnd();
console.groupCollapsed("Collapsed", 2);
console.log("in collapsed");
console.groupEnd();
console.groupEnd();
console.groupEnd(); // (one too many: nothing to undo)
console.log("outside");
