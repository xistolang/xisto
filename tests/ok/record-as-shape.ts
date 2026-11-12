// rows[0] as { R?: number } | undefined with rows: Record<string, unknown>[]: a copy in that shape (through JSON), null
// past the end as JavaScript reads undefined there
const rows: Record<string, unknown>[] = JSON.parse('[{"R":3,"TXT":"x"}]');
const vazias: Record<string, unknown>[] = [];
const row = rows[0] as { R?: number } | undefined;
const nada = vazias[0] as { R?: number } | undefined;
console.log(row?.R, nada?.R ?? 'sem', (rows[0] as { TXT?: string } | undefined)?.TXT);
const args: unknown[] = ['a'];
console.log(args[0] as string | undefined, (args[3] as string | undefined) ?? 'fora');
