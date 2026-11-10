const ops: Record<string, unknown>[] = [];
ops.push({ index: { _index: 'i-1' } });
ops.push({ a: 1, nested: { b: [1, 2], c: 'x' } });
console.log(JSON.stringify(ops));
const u: unknown = { k: 'v' };
console.log(JSON.stringify(u));
