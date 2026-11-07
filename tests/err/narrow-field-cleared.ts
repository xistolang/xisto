// expect: runtime error: the value is null
// a call that empties a narrowed field in between: the read is checked, so this stops instead of crashing
class Box { v: string | null = "x"; }
function clear(b: Box): int { b.v = null; return 0; }
const b = new Box();
if (b.v !== null) {
  const k = clear(b);
  console.log(b.v.length + k);
}
