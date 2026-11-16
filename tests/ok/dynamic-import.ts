// import("./x") with a literal path: x's namespace (m.f, or destructured), x's top level run on the first import(),
// not at start (and once); a module also imported statically ran at start; node: modules too
import { hi } from "./lib/dyn-shared.ts";
console.log("start", hi());
async function run(): Promise<void> {
  console.log("before");
  const { twice, LABEL } = await import("./lib/dyn-lazy.ts");
  console.log(twice(4), LABEL);
  const again = await import("./lib/dyn-lazy.ts");
  console.log(again.twice(5), again.LABEL);
  const s = await import("./lib/dyn-shared.ts");
  console.log(s.hi());
  await import("./lib/dyn-dep.ts").then((m) => {
    console.log("counter", m.counter, m.bump());
  });
  const path = await import("node:path");
  console.log(path.join("a", "b"));
}
run().then(() => console.log("done"));
