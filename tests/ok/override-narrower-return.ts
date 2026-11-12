// An override may return a narrower type than the method it overrides (boolean over boolean | Pending): a call
// through the base gets its value as the base's type. And `as` narrows a union to one of its members.
class Pending {
  constructor(readonly what: string) {}
}

class Gate {
  allow(n: number): boolean | Pending {
    return n > 0 ? true : new Pending(`n=${n}`);
  }
}

class StrictGate extends Gate {
  allow(n: number): boolean {
    const base = super.allow(n);
    if (base instanceof Pending) return false;
    return (base as boolean) && n < 10;
  }
}

const gates: Gate[] = [new Gate(), new StrictGate()];
for (const g of gates) {
  for (const n of [5, 50, -1]) {
    const r = g.allow(n);
    console.log(n, typeof r, r instanceof Pending ? `pending ${(r as Pending).what}` : String(r));
  }
}
const s = new StrictGate();
const direct: boolean = s.allow(3);
console.log(direct);
