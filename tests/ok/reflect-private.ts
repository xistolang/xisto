// Reflection adapters reach private members (the compiler's code), while user code outside the class cannot.
import { ClassInfo, MethodInfo } from "xisto:reflect";

const calls: MethodInfo[] = [];
function Track(): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo): void => {
    calls.push(m);
  };
}
function Service(): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    const made = c.construct(["secret-token"]);
    for (const m of calls) {
      console.log(m.name, m.invoke(made, []) as string);
    }
  };
}

@Service()
class Vault {
  constructor(private readonly token: string) {}
  @Track()
  peek(): string {
    return this.hidden();
  }
  private hidden(): string {
    return `${this.token.length} chars`;
  }
}
console.log("done");
