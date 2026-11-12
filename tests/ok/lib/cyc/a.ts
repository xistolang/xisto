import { B } from './b.ts';
export class A { nome = 'a'; filhos(): B[] { return [new B()]; } }
