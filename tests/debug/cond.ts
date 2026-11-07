interface Item { name: string; qty: int; }
const items: Item[] = [
  { name: "pen", qty: 2 },
  { name: "ink", qty: 0 },
  { name: "pad", qty: 7 },
  { name: "cap", qty: 4 },
];
function total(list: Item[]): int {
  let sum: int = 0;
  for (const it of list) {
    sum = sum + it.qty;
  }
  return sum;
}
function check(it: Item): int {
  if (it.qty === 0) {
    throw new Error(`no ${it.name} left`);
  }
  return it.qty;
}
let checked: int = 0;
try {
  for (const it of items) {
    checked = checked + check(it);
  }
} catch (e) {
  console.log("caught", e.message);
}
console.log("total", total(items));
const last = items[9];
console.log(last.name);
