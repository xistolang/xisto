// Record<string, string> (process.env) where Record<string, string | undefined> or Record<string, unknown> goes, a string
// enum's value where a string goes, { 1: x } as a Record<string, V>
function bypass(env: Record<string, string | undefined> = process.env): boolean {
  return (env['KIOSK_BYPASS_TESTE_XYZ'] ?? 'false') === 'true';
}
function campo(body: Record<string, unknown> | null): string {
  const v = body?.['j:x'];
  return typeof v === 'string' ? v : 'nada';
}
enum Tipo { AERO = 'AERONAUTICA', PEND = 'PENDENCIA' }
function log(descricao: string): string { return 'log:' + descricao; }
const corpo: Record<string, string> = { 'j:x': 'xml', outro: 'y' };
const ambiente: Record<string, string> = { KIOSK_BYPASS_TESTE_XYZ: 'true' };
console.log(bypass(), bypass(ambiente), campo(corpo), campo(null), log(Tipo.AERO), log(Tipo.PEND));
async function callPlsql(sql: string, binds: Record<string, unknown>): Promise<string> {
  return sql + ' ' + Object.keys(binds).join(',') + ' ' + JSON.stringify(binds);
}
async function principal(): Promise<void> {
  console.log(await callPlsql('BEGIN x(:1, :2); END;', { 1: 'a', 2: 7, 10: null }));
}
principal();
