// node:async_hooks: AsyncLocalStorage follows each run() through awaits, timers, Promise.all and interleaved tasks;
// nested runs, exit, enterWith; AsyncResource keeps the context where it was made (bind, runInAsyncScope).
import { AsyncLocalStorage, AsyncResource } from "node:async_hooks";
import { setTimeout as sleep } from "node:timers/promises";

class Ctx {
  id: string;
  constructor(id: string) {
    this.id = id;
  }
}

const als = new AsyncLocalStorage<Ctx>();
const other = new AsyncLocalStorage<string>();

function current(): string {
  const c = als.getStore();
  return c == null ? "none" : c.id;
}

const log: string[] = [];

async function handle(id: string, ms: int): Promise<string> {
  return als.run(new Ctx(id), async (): Promise<string> => {
    log.push("start " + id + "=" + current());
    await sleep(ms);
    log.push("mid " + id + "=" + current());
    const parts = await Promise.all([step(ms), step(ms + 5)]);
    log.push("end " + id + "=" + current());
    return id + ":" + parts.join("+");
  });
}

async function step(ms: int): Promise<string> {
  await sleep(ms);
  return current();
}

async function main(): Promise<void> {
  console.log("outside:", current());
  const r = als.run(new Ctx("sync"), (): string => current());
  console.log("sync run:", r, "after:", current());

  als.run(new Ctx("outer"), (): void => {
    console.log("outer:", current());
    als.run(new Ctx("inner"), (): void => {
      console.log("inner:", current());
      other.run("o1", (): void => {
        console.log("two stores:", current(), other.getStore());
      });
      als.exit((): void => {
        console.log("exit:", current());
      });
    });
    console.log("back to outer:", current(), other.getStore() == null ? "other unset" : "other set");
  });

  // concurrent "requests": each sees its own store across awaits
  const results = await Promise.all([handle("a", 30), handle("b", 10), handle("c", 20)]);
  console.log(results.join(" "));
  console.log(log.join(" | "));
  console.log("after all:", current());

  // AsyncResource: the context where the resource was made
  let bound: () => void = (): void => {};
  let res: AsyncResource = new AsyncResource("job");
  als.run(new Ctx("made"), (): void => {
    res = new AsyncResource("job");
    bound = AsyncResource.bind((): void => {
      console.log("static bind:", current());
    });
  });
  als.run(new Ctx("elsewhere"), (): void => {
    res.runInAsyncScope((): void => {
      console.log("runInAsyncScope:", current());
    });
    bound();
    console.log("still:", current());
  });

  // enterWith: the rest of this task sees the store
  await als.run(new Ctx("x"), async (): Promise<void> => {
    als.enterWith(new Ctx("entered"));
    await sleep(5);
    console.log("enterWith:", current());
  });
  console.log("end:", current());
}

main();
