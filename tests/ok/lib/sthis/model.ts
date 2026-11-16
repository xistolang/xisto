// a base class whose static methods name `this` and a helper of its own module
function tag(name: string): string { return "<" + name + ">"; }
export class Model {
  static table = "models";
  static describe(): string { return tag(this.table) + " " + this.kind(); }
  static kind(): string { return "model"; }
  static build(): Model { return new this(); }
  name(): string { return "Model"; }
}
