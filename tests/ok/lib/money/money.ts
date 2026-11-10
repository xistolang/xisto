export class Money {
  readonly cents: number;
  private constructor(cents: number) { this.cents = cents; }
  static of(n: number): Money { return new Money(n); }
  static readonly ZERO = new Money(0);
  plus(o: Money): Money { return new Money(this.cents + o.cents); }
}
