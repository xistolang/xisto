// Getters and setters, private / protected / readonly used as TypeScript allows.
class Temperature {
  private celsius: number = 0;
  readonly unit: string = "C";
  protected history: number[] = [];

  get fahrenheit(): number {
    return this.celsius * 1.8 + 32;
  }
  set fahrenheit(f: number) {
    this.setCelsius((f - 32) / 1.8);
  }
  get value(): number {
    return this.celsius;
  }
  set value(c: number) {
    this.setCelsius(c);
  }
  private setCelsius(c: number): void {
    this.history.push(this.celsius);
    this.celsius = c;
  }
  get label(): string {
    return `${this.celsius.toFixed(1)}${this.unit}`;
  }
}

class Thermostat extends Temperature {
  get label(): string {
    return `thermostat (${this.history.length} changes) now ${this.value.toFixed(1)}`;
  }
}

class Account {
  readonly id: int;
  private balance: int = 0;
  constructor(id: int) {
    this.id = id;
  }
  deposit(n: int): int {
    this.balance += n;
    return this.balance;
  }
  get total(): int {
    return this.balance;
  }
}

const t = new Temperature();
t.value = 100;
console.log(t.fahrenheit.toFixed(1), t.label, t.unit);
t.fahrenheit = 32;
console.log(t.value.toFixed(1), t.label);
t.value += 10;
t.value *= 2;
console.log(t.label);
console.log(t);

const th = new Thermostat();
th.value = 21;
th.value = 22.5;
const asBase: Temperature = th;
console.log(asBase.label, th.fahrenheit.toFixed(2));

const a = new Account(7);
a.deposit(5);
console.log(a.id, a.deposit(10), a.total);
