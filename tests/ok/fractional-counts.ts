function* gen(): Generator<number> { yield 1; yield 2; yield 3; yield 4; }
console.log(gen().drop(1.5).take(2).toArray(), gen().drop(-0.5).toArray());
const a: number[][][] = [[[1], [2]], [[3]]];
console.log(JSON.stringify(a.flat(1.5)), JSON.stringify(a.flat(0.5)), JSON.stringify(a.flat(2.2)));
console.log((255n).toString(36.5), (255n).toString(2.9), (-255n).toString(16.1));
try { gen().drop(-1); } catch (e) { console.log((e as Error).message); }
try { (1n).toString(1.9); } catch (e) { console.log((e as Error).message); }
