const u: unknown = 'x';
const z: unknown = '';
console.log(!u, !z, !!(null as unknown));
class Ds {}
const d = new Ds();
if (d) console.log('obj');
interface A { k: string; tag: string }
const xs = ['a', 'b'].map((k) => ({ k, tag: 't' }));
const typed: A[] = ['c'].map((k) => ({ k, tag: 'u' }));
console.log(xs.length, typed[0].k);
function take(p: unknown): string { return JSON.stringify(p); }
console.log(take(['s', 1, true]));
