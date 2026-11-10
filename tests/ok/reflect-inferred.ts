// Reflection of methods whose return type is left out: the descriptor has the type the checker inferred, and
// invoke returns the value (null for a method that returns nothing).
import { ClassInfo, MethodInfo } from "xisto:reflect";

const calls: MethodInfo[] = [];
function Track(): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {
    calls.push(m);
  };
}
function Service(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    const made = c.construct([]);
    for (const m of calls) {
      const r = m.invoke(made, []);
      console.log(m.name, m.returns.kind, m.returns.encode(r));
    }
  };
}

@Service()
class Painel {
  @Track()
  total() { return 42; }
  @Track()
  rotulo() { return "painel"; }
  @Track()
  resumo() { return { itens: 3, ok: true }; }
  @Track()
  limpa() { console.log("limpo"); }
}
console.log("done");
