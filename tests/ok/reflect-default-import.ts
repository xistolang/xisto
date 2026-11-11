// A decorator argument naming a default import (import cfg from "m"; @Tag(cfg.host)): the reflection code reads it as
// the module's default declaration, as the class's own code does.
import { ClassInfo } from "xisto:reflect";
import cfg from "./lib/kiosk/pkg-a.ts";
import { soma as somar } from "./lib/kiosk/pkg-a.ts";

function Tag(v: string, n: number): (c: ClassInfo) => void {
  return (c: ClassInfo): void => {
    console.log(c.name, v, n);
  };
}
@Tag(cfg.host, somar(2, 3))
class Servico {
  porta(): number {
    return cfg.port;
  }
}
console.log(new Servico().porta());
