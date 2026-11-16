// XB-134: type X = SomeClass (an imported class, here and in another module) is that class: values pass both ways
import { Order, Repository } from "./lib/alias-class/ent.ts";
import { mkRepo } from "./lib/alias-class/types.ts";
import type { OrderT, OrderRepo, Orders } from "./lib/alias-class/types.ts";
type Local = Order;
type Early = Later;
function f(p: Early): Later { return p; }
interface Store { save(o: OrderT): Promise<OrderT>; all(): Orders }
class Mem implements Store {
  os: Order[] = [];
  async save(o: Order): Promise<Order> { this.os.push(o); return o; }
  all(): Order[] { return this.os; }
}
class Later { n = 1; }
async function main() {
  const s: Store = new Mem();
  const o = new Order();
  o.items.push("a");
  const saved: Order = await s.save(o);
  const t: OrderT = saved;
  const l: Local = t;
  const all: Orders = s.all();
  const plain: Order[] = all;
  console.log(saved.total(), t.id, l === o, plain.length, f(new Later()).n);
  const repo: OrderRepo = mkRepo();
  repo.add(o);
  const r2: Repository<Order> = repo;
  console.log(r2.rows.length, r2 === repo);
  const m = new Map<number, OrderT>();
  m.set(1, o);
  const back: Order | null = m.get(1) ?? null;
  console.log(back?.total(), [o].map((x: Local): number => x.total()));
}
main();
