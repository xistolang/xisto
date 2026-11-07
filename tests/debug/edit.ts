enum Level { Low = 1, High = 2 }
enum Mode { Fast = "fast", Safe = "safe" }
interface Box { label: string; size: int; note: string | null; }
function describe(b: Box, prefix: string, level: Level, mode: Mode): string {
  const scale: int = 10;
  let factor: number = 1.5;
  let enabled: boolean = true;
  let count: int | null = 3;
  const area = b.size * scale;
  const shown = count === null ? -1 : count;
  const note = b.note || "-";
  return `${prefix} ${b.label} ${area} ${factor} ${enabled} ${shown} ${level} ${mode} ${note}`;
}
const box: Box = { label: "crate", size: 4, note: "fragile" };
const items: int[] = [1, 2, 3];
console.log(describe(box, "box:", Level.Low, Mode.Fast));
console.log(items);
