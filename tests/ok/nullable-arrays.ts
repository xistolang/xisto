interface User {
  name: string;
}
const names: (string | null)[] = ["ana", null, "bia"];
console.log(names, names.length, names[1], names[0]);
names.push(null);
names.push("caio");
console.log(names.join(","), names.includes(null), names.indexOf("bia"));
const present = names.filter((n: string | null): boolean => n !== null);
console.log(present);
const lens = names.map((n: string | null): int => (n === null ? 0 : n.length));
console.log(lens);
for (const n of names) {
  if (n !== null) {
    console.log("hi", n);
  }
}
const users: Array<User | null> = [{ name: "x" }, null];
console.log(users, JSON.stringify(users));
const back: (User | null)[] = JSON.parse(`[null, {"name":"y"}]`);
console.log(back);
const grid: (string[] | null)[] = [["a"], null];
console.log(grid);
names[0] = null;
console.log(names);
