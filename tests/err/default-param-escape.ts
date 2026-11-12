// expect: and 'f' of guarda lets it out: an adapter would not be the same function
class Guarda {
  f: ((s: string, n?: number) => string) | null = null;
  guarda(f: (s: string, n?: number) => string): void {
    this.f = f;
  }
}
const g = (s: string, n: number = 1): string => s + n;
const k = new Guarda();
k.guarda(g);
