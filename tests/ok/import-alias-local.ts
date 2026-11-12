import { Conta, ambas } from './lib/alias/mid.ts';
console.log(new Conta().w, ambas());
try { decodeURIComponent('%zz'); } catch (e) {
  const err = e as Error;
  console.log(err.name, err.message, err instanceof Error);
}
console.log(decodeURIComponent('a%20b'));
