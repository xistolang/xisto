// a class whose static fields other modules assign (static-assign-cross-module.ts)
export class Hooks {
  static keepTrace: (name: string) => boolean = (name: string) => true;
  static onEnd: ((n: number) => void) | null = null;
  static count: number = 0;
  static label: string = "default";
  static ratio = 0;
  static readonly version: string = "1.0";

  static check(name: string): boolean {
    return Hooks.keepTrace(name);
  }
  static finish(n: number): void {
    Hooks.count += n;
    const f = Hooks.onEnd;
    if (f !== null) f(Hooks.count);
  }
}
