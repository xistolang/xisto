export class Logger {
  private ctx: string;
  constructor(ctx: string) { this.ctx = ctx; }
  hello(): string { return "nest " + this.ctx; }
}
