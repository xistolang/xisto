/* The Computer Language Benchmarks Game
   https://salsa.debian.org/benchmarksgame-team/benchmarksgame/

   contributed by Denis Gribov
   based on C source code by Ledhug
   un-buffered by Isaac Gouy
   (xisto: the same program with types; node/pidigits-node-3.js is the original. `last ? res += ' ' : res = '0' + res`
   is an if/else here)
*/

const n = process.argv.length > 2 ? Math.trunc(Number(process.argv[2])) : 10000; // (+argv[2] || 10000)

let i = 0, ns: int = 0;
let k = 0;
let k2 = 1;
let acc = 0n;
let den = 1n;
let num = 1n;
let tmp = 0n;
let d3 = 0n;
let d4 = 0n;

function pad(i: int, last: boolean): string {
  let res = i.toString(), count: int = 0;
  count = 10 - res.length;
  while (count > 0) {
    if (last) res += " "; else res = "0" + res;
    count--;
  }
  return res;
}

while (i < n) {
  k++;
  k2 += 2;

  acc += num * 2n;
  acc *= BigInt(k2);
  den *= BigInt(k2);
  num *= BigInt(k);

  if (num > acc) {
    continue;
  }

  tmp = num * 3n;
  tmp = tmp + acc;
  d3 = tmp / den;

  tmp = tmp + num;
  d4 = tmp / den;

  if (d3 !== d4) {
    continue;
  }

  const d = Math.trunc(Number(d3));
  ns = ns * 10 + d;
  i++;
  const last = i >= n;
  if (i % 10 === 0 || last) {
    console.log(pad(ns, last) + "\t:" + String(i));
    ns = 0;
  }

  if (last) break;

  acc -= den * BigInt(d);
  acc *= 10n;
  num *= 10n;
}
