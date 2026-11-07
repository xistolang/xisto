class Ring {
  name: string;
  next: Ring | null = null;
  constructor(name: string) {
    this.name = name;
  }
}
const a = new Ring("a");
const b = new Ring("b");
a.next = b;
b.next = a;
console.log("linked");
a.next = null;
