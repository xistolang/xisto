// renamed re-exports, export { a as b }, export default of a value and of a name, default imports under another name
import { PK_A_ORIGINAL, somar, normalizaTexto, principalB } from './lib/kiosk/index.ts';
import { origemB } from './lib/kiosk/origem-b.ts';
import redisConfig from './lib/kiosk/pkg-a.ts';
import principal from './lib/kiosk/pkg-b.ts';
import { soma as somaDireta } from './lib/kiosk/pkg-a.ts';

console.log(PK_A_ORIGINAL, origemB, somar(2, 3), somaDireta(1, 1));
console.log(normalizaTexto('  abc '), principalB(7), principal(8));
console.log(redisConfig.host, redisConfig.port);
