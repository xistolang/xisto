// A module whose other exports need the event loop, the coroutines or the cleanup stack (timers, fetch, async,
// generators, try): a program that uses only twice() must not get them (XB-250).
export function twice(x: number): number {
  return x * 2;
}

export function later(ms: number): void {
  setTimeout(() => console.log("later"), ms);
}

export async function get(url: string): Promise<number> {
  const r = await fetch(url);
  return r.status;
}

export function* count(n: number): Generator<number> {
  for (let i = 0; i < n; i++) yield i;
}

export function safeParse(s: string): number {
  try {
    return JSON.parse(s) as number;
  } catch (e) {
    return -1;
  }
}

export class Ticker {
  n = 0;
  start(): void {
    setInterval(() => { this.n++; }, 10);
  }
}
