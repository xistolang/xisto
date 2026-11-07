// Promise.all / race run their elements concurrently: three 150 ms waits take ~150 ms, not 450. Also .map,
// objects as results, an error from one task, race (the loser finishes in the background before the program
// exits, as in Node), nested groups, and values captured by the tasks.
import { setTimeout as sleep } from "node:timers/promises";

interface User {
  id: int;
  name: string;
}
const log: string[] = [];
async function load(id: int, ms: int): Promise<User> {
  await sleep(ms);
  log.push("loaded " + String(id));
  return { id: id, name: "user" + String(id) };
}
async function slowSquare(x: int): Promise<int> {
  await sleep(100 + (5 - x) * 10);
  return x * x;
}
async function fail(ms: int): Promise<int> {
  await sleep(ms);
  throw new Error("boom after " + String(ms));
}

async function main(): Promise<void> {
  let t0 = Date.now();
  const users = await Promise.all([load(1, 150), load(2, 120), load(3, 90)]);
  const took = Date.now() - t0;
  console.log(users.map((u: User): string => u.name).join(","), took < 300 ? "concurrent" : "sequential:" + String(took));
  console.log(log.join(" | "));

  t0 = Date.now();
  const nums: int[] = [1, 2, 3, 4, 5];
  const squares = await Promise.all(nums.map((x: int) => slowSquare(x)));
  console.log(squares.join(","), Date.now() - t0 < 300 ? "concurrent" : "sequential");

  const prefix = "item-";
  const names = await Promise.all(["a", "b"].map(async (s: string): Promise<string> => {
    await sleep(20);
    return prefix + s;
  }));
  console.log(names.join(" "));

  try {
    await Promise.all([slowSquare(1), fail(30), slowSquare(2)]);
    console.log("not reached");
  } catch (e) {
    console.log("caught:", (e as Error).message);
  }

  const first = await Promise.race([load(10, 200), load(11, 50)]);
  console.log("race won by", first.name);

  const nested = await Promise.all([
    Promise.all([slowSquare(1), slowSquare(2)]),
    Promise.all([slowSquare(3), slowSquare(4)]),
  ]);
  console.log(nested.map((r: int[]): string => r.join("+")).join(" ; "));
  console.log(await Promise.resolve(42));
}
main();
