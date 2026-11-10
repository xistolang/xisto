function send(o: object): string { return JSON.stringify(o); }
const doc: Record<string, unknown> = { a: 1, b: 'x', c: [1, 2], d: null };
const ops: Record<string, unknown>[] = [doc, { e: true }];
console.log(send({ operations: ops, refresh: false }));
console.log(send(doc));
