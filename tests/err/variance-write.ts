// expect: Map<string, number> goes where Map<string, unknown> goes, and 'binds' of grava may write it
// build: --tsc-checked
function grava(binds: Record<string, unknown>): void {
  binds['x'] = 1;
}
const nums: Record<string, number> = { a: 1, b: 2 };
grava(nums);
