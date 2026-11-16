// for (let k ...): each iteration has its own k, also when a closure changes it (XB-212)
function main(): void {
  const busy: int[] = [];
  const fs: (() => void)[] = [];
  const gs: (() => int)[] = [];
  const scan = (): int => { let t: int = 0; for (let k = 0; k < busy.length; k++) { t += busy[k]; } return t; };
  const scan2 = (): int => { let t: int = 0; for (let k = 0; k < busy.length; k++) { gs.push((): int => k); t += busy[k]; } return t; };
  for (let k = 0; k < 2; k++) {
    busy.push(0);
    fs.push((): void => { busy[k % 2]--; console.log("k", k, scan(), scan2()); k += 10; });
  }
  fs[0]();
  fs[1]();
  fs[0]();
  console.log(gs.map((g) => g()).join(","));
}
main();

// at the top level, a string variable, continue, and a loop without an update
const hs: (() => string)[] = [];
for (let s = "a"; s.length < 5; s += "b") {
  if (s.length === 2) continue;
  hs.push((): string => { s += "!"; return s; });
}
for (const h of hs) console.log(h(), h());
const ns: (() => number)[] = [];
for (let n = 0; n < 3;) {
  ns.push((): number => n * 10);
  n++;
}
console.log(ns.map((f) => f()).join(","));

// a counter changed inside the body, seen by the closure made after the change
const ws: (() => int)[] = [];
for (let i: int = 0; i < 6; i++) {
  if (i % 2 === 0) i++;
  ws.push((): int => i);
}
console.log(ws.map((f) => f()).join(","));

// async: each callback keeps the iteration's value after the awaits
async function later(): Promise<void> {
  const ps: Promise<void>[] = [];
  for (let k = 0; k < 3; k++) {
    ps.push(new Promise<void>((res) => setTimeout(() => { k += 100; console.log("later", k); res(); }, 3 - k)));
  }
  await Promise.all(ps);
}
later();
