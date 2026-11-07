class Conta {
  saldo: int = 0;
  sacar(v: int): void {
    if (v > this.saldo) {
      throw new Error("saldo insuficiente");
    }
    this.saldo = this.saldo - v;
  }
}
interface Item {
  id: int;
}
function show(label: string, e: Error): void {
  console.log(`-- ${label}`);
  console.log(e.stack);
}
try {
  new Conta().sacar(10);
} catch (e) {
  show("method", e);
}
try {
  const src: int[] = [1, 2];
  const xs = src.map((x: int): int => x * 4611686018427387904 * 4);
  console.log(xs.length);
} catch (e) {
  show("arrow", e);
}
try {
  const it: Item = JSON.parse("{\"id\": \"x\"}");
  console.log(it.id);
} catch (e) {
  show("json", e);
}
try {
  const a: int[] = [];
  a.pop();
} catch (e) {
  show("pop", e);
}
try {
  console.log("ab".repeat(-1));
} catch (e) {
  show("repeat", e);
}
try {
  let i: int = 4611686018427387904 - 1 + 4611686018427387904;
  i++;
} catch (e) {
  show("postfix", e);
}
try {
  let j: int = 4611686018427387904;
  j += j;
  j += 1;
} catch (e) {
  show("compound", e);
}
try {
  const z: int = 0;
  console.log(7 % z);
} catch (e) {
  show("modulo", e);
}
try {
  console.log(Math.floor(1e300));
} catch (e) {
  show("floor", e);
}
const e2 = new Error("not thrown");
console.log(e2.stack);
