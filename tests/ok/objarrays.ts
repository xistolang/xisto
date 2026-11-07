// Arrays of objects and strings, push/pop/splice, JSON round trips.
class Item {
  id: int;
  name: string;
  price: number;
  tags: string[] = [];
  constructor(id: int, name: string, price: number) {
    this.id = id;
    this.name = name;
    this.price = price;
  }
}

const names: string[] = ["b", "a"];
names.push("c");
console.log(names, names.length, names[0] + names[2]);
const last: string = names.pop();
console.log(last, names.length);
names.splice(0, 1);
console.log(names);

const items: Item[] = [];
for (let i: int = 1; i <= 3; i++) {
  const it: Item = new Item(i, "item" + String(i), Number(i) * 1.5);
  it.tags.push("t" + String(i));
  items.push(it);
}
items[1].tags.push("extra");
console.log(items.length, items[1].name, items[1].tags, items[2].price);
const json: string = JSON.stringify(items);
console.log(json);
const back: Item[] = JSON.parse(json);
console.log(back.length, back[2].name, back[2].tags, JSON.stringify(back) === json);
const one: Item = JSON.parse("{\"id\": 7, \"name\": \"sete \\\"q\\\" \\u00e1\", \"price\": 0.5, \"tags\": [\"x\"]}");
console.log(one.id, one.name, one.price, one.tags, JSON.stringify(one));
console.log(JSON.stringify("a\"b\n"), JSON.stringify(1.5), JSON.stringify(true), JSON.stringify([1, 2]), JSON.stringify(["x", "y"]));
const nums: number[] = JSON.parse("[1, 2.5, -3]");
console.log(nums, Number("42"), Number(" 1.5 "), Number("x"), Number(""), Number.isNaN(Number("x")), Number.isInteger(2.5), Number.isInteger(3));
items.splice(1, 1);
console.log(items.length, items[1].id);
const filled: string[] = new Array<string>(3).fill("z");
console.log(filled, filled.length);
