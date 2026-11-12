// Record<Enum, V> with computed enum keys (string and numeric enums)
enum Marco { INICIO = 'INICIO', FIM = 'FIM' }
enum Nivel { BAIXO, ALTO }
const LITERAL: Record<Marco, string> = { [Marco.INICIO]: 'início', [Marco.FIM]: 'fim' };
const PESO: Record<Nivel, number> = { [Nivel.BAIXO]: 1, [Nivel.ALTO]: 10 };
function texto(m: Marco): string { return LITERAL[m] ?? ''; }
console.log(texto(Marco.FIM), PESO[Nivel.ALTO], Object.keys(LITERAL).join(','));
