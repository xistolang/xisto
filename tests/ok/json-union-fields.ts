// JSON into Date | string fields (a row's date column), object decoded as a number, rows[0] as { R?: unknown } | undefined,
// Record<string, string> as Record<string, unknown>
interface Linha { DT_EXAME: Date | string; N: number | null }
function toDate(v: Date | string): Date { return v instanceof Date ? v : new Date(v); }
const raw: object = JSON.parse('[{"DT_EXAME":"2026-11-12T10:00:00.000Z","N":1},{"DT_EXAME":"2026-11-13T00:00:00.000Z","N":null}]');
const rows = raw as Linha[];
console.log(rows.map((r) => toDate(r.DT_EXAME).toISOString()).join(','), rows[1].N, JSON.stringify(rows[0]));
const raw2: object = JSON.parse('7');
const n: number = raw2 as number;
const rows2: Record<string, unknown>[] = [{ R: 'x' }];
const v = (rows2[0] as { R?: unknown } | undefined)?.R;
const parm: Record<string, string> = { ECG: 'ON' };
const valor = (parm as Record<string, unknown>)?.['ECG'];
console.log(n + 1, v, valor);
