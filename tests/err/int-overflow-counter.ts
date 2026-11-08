// expect: runtime error: integer overflow: the result left JavaScript's safe integer range
// the counting idiom with a constant step tests the safe range after the loop (the values only rise): the third
// add of 2^52 passes 2^53, and the error comes before anything reads the map
function tally(keys: Uint8Array): Map<int, int> {
  const m = new Map<int, int>();
  for (let i = 0; i < keys.length; i++) {
    const k: int = keys[i];
    if (m.has(k)) m.set(k, m.get(k)! + 4503599627370496);
    else m.set(k, 4503599627370496);
  }
  return m;
}
const m = tally(new Uint8Array([7, 7, 7, 1]));
console.log(m.get(7)!);
