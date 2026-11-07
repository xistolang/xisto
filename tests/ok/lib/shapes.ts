export class Registry {
  static created: int = 0;
  static readonly label: string = "registry";
  static bump(): int {
    Registry.created = Registry.created + 1;
    return Registry.created;
  }
}

export class Base {
  readonly id: int;
  log: string[] = ["Base field"];
  constructor(id: int) {
    this.log.push("Base constructor");
    this.id = id;
    Registry.bump();
  }
  name(): string {
    return "base";
  }
  greet(): string {
    return `I am ${this.name()} #${this.id}`;
  }
}
