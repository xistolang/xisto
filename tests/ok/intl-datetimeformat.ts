// Intl.DateTimeFormat (xisto:intl): en-GB, en-US, pt-BR; numeric and 2-digit fields, hourCycle/hour12, a timeZone of the
// TZ database; format and formatToParts as Node gives them (and reduce<Record<..>>(f, {}) with the type given)
const TZ_ATENDIMENTO = 'America/Fortaleza';
function partesBRT(now: Date): Record<string, string> {
  return new Intl.DateTimeFormat('en-GB', {
    timeZone: TZ_ATENDIMENTO,
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hourCycle: 'h23',
  })
    .formatToParts(now)
    .reduce<Record<string, string>>((acc, p) => {
    acc[p.type] = p.value;
    return acc;
  }, {});
}
function formatDataBRT(now: Date): string {
  const p = partesBRT(now);
  return `${p['year']}-${p['month']}-${p['day']}`;
}
function formatHoraBRT(now: Date): string {
  const p = partesBRT(now);
  return `${p['hour']}:${p['minute']}:${p['second']}`;
}
const instantes = [Date.UTC(2026, 10, 12, 2, 3, 4), Date.UTC(2026, 2, 5, 14, 7, 9), Date.UTC(2025, 0, 1, 0, 0, 0)];
for (const ms of instantes) {
  const d = new Date(ms);
  console.log(formatDataBRT(d), formatHoraBRT(d));
  for (const loc of ['en-GB', 'en-US', 'pt-BR']) {
    const opts: Intl.DateTimeFormatOptions[] = [
      { timeZone: 'UTC' },
      { timeZone: 'UTC', year: '2-digit', month: '2-digit', day: '2-digit' },
      { timeZone: 'UTC', hour: 'numeric', minute: 'numeric', second: 'numeric' },
      { timeZone: 'America/Sao_Paulo', hour: '2-digit', minute: '2-digit', hour12: false },
      { timeZone: 'Asia/Kolkata', year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: '2-digit' },
      { timeZone: 'UTC', year: 'numeric', month: '2-digit' },
      { year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit', hourCycle: 'h23' },
    ];
    for (const o of opts) console.log(loc, JSON.stringify(new Intl.DateTimeFormat(loc, o).format(d)));
  }
}
console.log(JSON.stringify(new Intl.DateTimeFormat('pt-BR', { timeZone: 'America/Fortaleza', hour: '2-digit', minute: '2-digit' }).formatToParts(new Date(instantes[0]))));
