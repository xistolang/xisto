export function check(v: int): int {
  if (v < 0) {
    throw new Error(`negative: ${v}`);
  }
  return v;
}
