// Promise.all / Promise.race over xs.map(x => f(x).then(...)): the callback gives a promise of its own (not an
// async call), whose value is what the array holds (XB-230)
interface User {
  id: number;
  name: string;
}

async function load(id: number): Promise<User> {
  return { id, name: `u${id}` };
}

async function twice(x: number): Promise<number> {
  return x * 2;
}

const ids = [1, 2, 3];
const names = await Promise.all(ids.map((id) => load(id).then((u) => u.name)));
console.log(names, `${names[0]}`);
console.log(await Promise.all(ids.map((id) => load(id).then((u) => ({ ...u, tag: "t" })))));
console.log(await Promise.all(ids.map((x) => twice(x).then((n) => ({ n })))));
console.log(await Promise.all(ids.map((id) => load(id).then((u) => `${u.name}!`).then((s) => s.length))));
const kept = ids.map((x) => twice(x).then((v) => `v${v}`));
console.log(await Promise.all(kept));
console.log(await Promise.race(ids.map((x) => twice(x).then((v) => `first ${v}`))));
console.log(await Promise.all(ids.map(async (id) => await load(id).then((u) => u.id))));
