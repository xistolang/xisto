// XB-212: a closure that captures a for-let `k` keeps one copy per iteration even when an earlier closure of the same
// function declares its own loop variable `k` (that one is another variable)
function main(): void {
  const busy: number[] = [];
  const fs: (() => void)[] = [];
  const scan = (): number => { let t = 0; for (let k = 0; k < busy.length; k++) t += busy[k]; return t; };
  for (let k = 0; k < 2; k++) {
    busy.push(0);
    fs.push((): void => { busy[k]--; console.log("k", k, scan()); });
  }
  fs[0]();
  fs[1]();
}
main();
// a closure that really reassigns the captured variable still shares one cell
function counter(): void {
  let n = 0;
  const inc = (): void => { for (let n = 0; n < 1; n++) {} };
  const bump = (): number => ++n;
  inc();
  bump();
  console.log(bump());
}
counter();
