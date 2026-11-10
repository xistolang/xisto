// Each rule warning (XB-118): the program compiles, xisto check lists them apart.
function first<T>(xs: T[]): T | null {
  return xs.length > 0 ? xs[0] : null;
}
function empty<T>(): T[] {
  return [];
}

const items: string[] = ['a', 'b'];
const total: number = 2.5;
const count = items.length;

const mixed = count * total; // rule 1: int mixed with number
const none: string | null = undefined; // rule 2: undefined read as null
const xs = empty(); // rule 3: T not inferred
const n: number = 3;
if (n === null) console.log('never'); // rule 4: never null
if (!items.length) console.log('empty'); // rule 5: int as a condition

console.log(mixed, none ?? 'none', xs.length, first(items));
