// XB-216: reading through a hole of an array of arrays (or of objects) is JavaScript's TypeError, catchable, with
// Node's message (it was a segmentation fault, then a run-time error that could not be caught)
class P { x = 1; hi(): string { return "hi"; } }
const X: number[][] = new Array<number[]>(3);
X[0] = [1, 2];
console.log(X[0][1]);
function report(e: unknown): void { console.log((e as Error).name + ": " + (e as Error).message); }
try { console.log(X[1][0]); } catch (e) { report(e); }
const i: int = 2;
try { console.log(X[i][i]); } catch (e) { report(e); }
try { console.log(X[1].length); } catch (e) { report(e); }
try { X[2][0] = 5; } catch (e) { report(e); }
try { X[2][i] = 5; } catch (e) { report(e); }
try { X[2][0] += 5; } catch (e) { report(e); }
const ps: P[] = new Array<P>(2);
ps[0] = new P();
console.log(ps[0].x, ps[0].hi());
try { console.log(ps[1].x); } catch (e) { report(e); }
try { console.log(ps[1].hi()); } catch (e) { report(e); }
try { ps[1].x = 3; } catch (e) { report(e); }
// filled rows read as usual, also in a loop
const M: number[][] = new Array<number[]>(4);
for (let r = 0; r < 4; r++) { M[r] = []; for (let c = 0; c < 4; c++) M[r].push(r * c); }
let s = 0;
for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) s += M[r][c];
console.log(s);
