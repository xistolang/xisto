// a callback parameter written with references where the caller's type says any (passport's done): one closure serves both
type Provider = (req: string, raw: string, done: (err: any, key?: any) => void) => void;
function useProvider(p: Provider): string {
  let out = "";
  p("r", "t", (err: any, key?: any): void => {
    out = err !== null ? `err ${(err as Error).message}` : `key ${String(key)}`;
  });
  return out;
}
const off: Provider = (_req: any, _raw: any, done: (err: Error | null, key?: any) => void) => {
  done(null, false);
};
const bad: Provider = (_req: any, _raw: any, done: (err: Error | null, key?: any) => void) => {
  done(new Error("no jwks"), null);
};
console.log(useProvider(off));
console.log(useProvider(bad));
