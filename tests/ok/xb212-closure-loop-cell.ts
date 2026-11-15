// XB-212: a closure that captures the k of a for (let k ...) keeps its own k per iteration even when other closures
// of the function (before or after it, nested deeper, or at module level) declare loops of their own with a k
function main(): void {
  const busy: int[] = [];
  const fs: (() => void)[] = [];
  const scan = (): int => { let t: int = 0; for (let k = 0; k < busy.length; k++) t += busy[k]; return t; };
  const scan2 = (): int => { let t: int = 0; const inner = (): void => { for (let k = 0; k < busy.length; k++) t += busy[k]; }; inner(); return t; };
  for (let k = 0; k < 2; k++) {
    busy.push(0);
    fs.push((): void => { busy[k]--; console.log("k", k, scan(), scan2()); });
  }
  const after = (): int => { let s = 0; for (let k = 0; k < 3; k++) s += k; return s; };
  for (let k = 0; k < 2; k++) fs.push((): void => { console.log("k2", k, after()); });
  for (const f of fs) f();
}
main();
const gs: (() => void)[] = [];
const scanTop = (): int => { let t: int = 0; for (let k = 0; k < 3; k++) t += k; return t; };
for (let k = 0; k < 3; k++) gs.push((): void => { console.log("top", k, scanTop()); });
for (const g of gs) g();
