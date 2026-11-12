// a module shaped like mongoose's: a class, and a namespace-like class whose name qualifies it (Types.Oid)
export class Oid {
  private readonly hex: string;
  constructor(hex: string) {
    this.hex = hex.toLowerCase();
  }
  toString(): string {
    return this.hex;
  }
  toJSON(): string {
    return this.hex;
  }
}
export class Types {
  static readonly label: string = 'types';
}
export type Filter<T> = Record<string, unknown>;
