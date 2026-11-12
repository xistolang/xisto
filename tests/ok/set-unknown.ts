// Set<unknown>: JavaScript's SameValueZero (strings and numbers by value, NaN equals NaN, -0 is 0, objects by identity,
// null a value), insertion order, has/add/delete/clear/size/values/spread/for...of, printing
// build: --tsc-checked
const s = new Set<unknown>();
const o = { a: 1 };
s.add('x'); s.add(1); s.add(1.0); s.add(NaN); s.add(NaN); s.add(-0); s.add(0); s.add(true); s.add(o); s.add({ a: 1 }); s.add(null); s.add('x');
console.log(s.size, s.has('x'), s.has(1), s.has(NaN), s.has(0), s.has(-0), s.has(o), s.has({ a: 1 }), s.has(null), s.has(false));
console.log(s.delete(1), s.has(1), s.size);
console.log([...s].length, Array.from(s.values()).map((v) => typeof v).join(','));
for (const v of s) if (typeof v === 'string') console.log('str', v);
function unwrap(err: unknown): number {
  let cur: unknown = err;
  const seen = new Set<unknown>();
  let n = 0;
  while (cur && typeof cur === 'object' && 'cause' in cur && (cur as { cause?: unknown }).cause && !seen.has(cur)) {
    seen.add(cur);
    cur = (cur as { cause?: unknown }).cause;
    n++;
  }
  return n;
}
console.log(unwrap(new Error('a', { cause: new Error('b', { cause: new Error('c') }) })));
const t = new Set<unknown>(['a', 2, 'a']);
console.log(t.size);
s.clear();
console.log(s.size);
const p = new Set<unknown>(['a', 1, null, { b: 2 }]);
console.log(p);
const q = new Set<unknown>();
console.log(q);
