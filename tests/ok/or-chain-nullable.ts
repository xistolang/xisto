const a: string | null = null, b: string | null = 'b', c: string | null = '';
console.log(a || b || 'x', a || c || 'y', c || null, a || a);
function f(): string | null { return ''; }
console.log(f() || b || 'z');
