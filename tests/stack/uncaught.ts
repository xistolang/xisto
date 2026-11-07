import { check } from "./lib.ts";

class Pedido {
  total: int;
  constructor(total: int) {
    this.total = check(total);
  }
}
function criar(): Pedido {
  return new Pedido(-5);
}
console.log(criar().total);
