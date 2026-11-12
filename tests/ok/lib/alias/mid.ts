import { Conta as ContaLib } from './lib.ts';
export class Conta { w = 'local'; }
export const ambas = (): string => new Conta().w + '/' + new ContaLib().v;
