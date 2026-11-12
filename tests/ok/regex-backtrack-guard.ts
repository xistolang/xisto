// build: --tsc-checked
// the backtracking engine's guard: a pattern that backtracks exponentially stops with an error (V8 would run on)
const s = 'a'.repeat(28) + 'c';
try {
  console.log(/^(a+)+\1b$/.test(s));
} catch (e) {
  console.log((e as Error).name, (e as Error).message.startsWith('Maximum backtracking steps exceeded'));
}
console.log(/^(a+)+\1b$/.test('aaaab'), /(a)\1/.test('xaay'));
