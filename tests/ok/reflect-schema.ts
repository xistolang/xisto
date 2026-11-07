import { ClassInfo, MethodInfo } from "xisto:reflect";

enum Role { Admin = "admin", User = "user" }
interface Profile { username: string; bio: string | null; following: boolean; }
interface Article { slug: string; tagList: string[]; favoritesCount: int; author: Profile; parent?: Article; score: number; role: Role; }
interface Wrapped<T> { item: T; total: int; }

function Show(): (c: ClassInfo, m: MethodInfo) => void {
  return (c: ClassInfo, m: MethodInfo) => {
    console.log(c.name + "." + m.name + " returns " + m.returns.schema);
    for (const p of m.params) { console.log("  " + p.name + ": " + p.type.schema); }
  };
}

class Api {
  @Show()
  one(slug: string, limit: int, tags: string[] | null): Wrapped<Article> { throw new Error("x"); }
  @Show()
  two(on: boolean): Map<string, number> { return new Map<string, number>(); }
}
console.log("done");
