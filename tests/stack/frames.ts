// One failing helper reached through every kind of frame; the case is chosen by argv[2].
function bad(i: int): int {
  const a: int[] = [1];
  return a[i];
}
function add(i: int): int {
  return i + 4611686018427387904;
}
const big: int[] = [5];
class Box {
  v: int = bad(big[0]);
  w: int = 0;
  constructor(x: int) {
    this.w = x;
  }
  get(i: int): int {
    return bad(i);
  }
}
class Bag {
  v: int = big[3];
}
function viaFilter(): int[] {
  const q: int[] = [1, 2, 3];
  return q.filter((x: int): boolean => bad(x) > 0);
}
function viaSome(): boolean {
  const q: int[] = [3];
  return q.some((x: int): boolean => bad(x) > 0);
}
function viaSort(): void {
  const xs: int[] = [3, 1, 2];
  xs.sort((a: int, b: int): int => bad(a) - b);
}
function viaForEach(): void {
  const xs: int[] = [4];
  xs.forEach((x: int): void => {
    bad(x);
  });
}
function makeBox(): int {
  return new Box(1).v;
}
function makeBag(): int {
  return new Bag().v;
}
function twice(v: int): int {
  return add(v) + add(0);
}
const which: string = process.argv[2];
if (which === "filter") {
  console.log(viaFilter().length);
}
if (which === "some") {
  console.log(viaSome());
}
if (which === "sort") {
  viaSort();
}
if (which === "foreach") {
  viaForEach();
}
if (which === "method") {
  console.log(new Box(0).get(4));
}
if (which === "field-ctor") {
  console.log(makeBox());
}
if (which === "field") {
  console.log(makeBag());
}
if (which === "clone") {
  console.log(add(1), twice(4611686018427387904));
}
