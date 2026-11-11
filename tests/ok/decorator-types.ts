// TypeScript's decorator types (MethodDecorator, ClassDecorator, PropertyDecorator) as xisto's, and decorator
// factories held in a const (export const Policy = (p: P): MethodDecorator => SetMetadata(KEY, p))
import { ClassInfo, FieldInfo, MethodInfo } from "xisto:reflect";

const metas: string[] = [];
function Meta(k: string, v: string): (c: ClassInfo, m: MethodInfo | null) => void {
  return (c: ClassInfo, m: MethodInfo | null): void => {
    metas.push(`${c.name}.${m === null ? "-" : m.name} ${k}=${v}`);
  };
}
type Permissao = "admin" | "it";
export const Politica = (p: Permissao): MethodDecorator => Meta("politica", p);
export const Escopo = (r: string = "local"): ClassDecorator => Meta("escopo", r);
function Campo(rotulo: string): PropertyDecorator {
  return (c: ClassInfo, f: FieldInfo): void => {
    metas.push(`${c.name}.${f.name} campo=${rotulo}`);
  };
}

@Escopo()
class Grupos {
  @Politica("admin")
  salvar(): string {
    return "salvo";
  }
  @Campo("Nome")
  nome: string = "";
}
@Escopo("ponto")
class Pontos {
  @Politica("it")
  apagar(): string {
    return "apagado";
  }
}
console.log(new Grupos().salvar(), new Pontos().apagar());
console.log(metas.join("\n"));
