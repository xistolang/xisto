// expect: are both declared as 'ORIGINAL': one module cannot import both
import { PK_A_ORIGINAL, PK_B_ORIGINAL } from '../ok/lib/kiosk/index.ts';
console.log(PK_A_ORIGINAL, PK_B_ORIGINAL);
