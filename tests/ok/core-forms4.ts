// core (round 8): f() as Promise<PageDto<B>> with f giving PageDto<A> (two instances of one generic class: a copy), and
// [...seed, ...page.items] of two element shapes (the shape both copy into)
// build: --tsc-checked
class PageDto<T> {
  items: T[] = [];
  total: number = 0;
}
class Ident { sub: string = ''; email?: string; createdAt?: string; }
class IdentDto { sub: string = ''; email?: string; }
async function list(): Promise<PageDto<Ident>> {
  const p = new PageDto<Ident>();
  const i = new Ident(); i.sub = 'a'; p.items.push(i); p.total = 1;
  return p;
}
async function ctrl(): Promise<PageDto<IdentDto>> {
  return list() as Promise<PageDto<IdentDto>>;
}
async function main(): Promise<void> {
  const r = await ctrl();
  console.log(r.total, r.items.map((x) => x.sub).join(','), r.items[0].email ?? '-');
}
await main();
class CreateScreenDto { id: string = ''; title: string = ''; }
class ScreenResponseDto { id: string = ''; title: string = ''; createdAt: string = ''; }
const SEED = { screens: [{ id: 's1', title: 'A' }] as CreateScreenDto[] };
async function listScreens(p: number): Promise<{ items: ScreenResponseDto[]; pages: number }> {
  const r = new ScreenResponseDto(); r.id = 'r' + p; r.title = 'T';
  return { items: [r], pages: 2 };
}
async function options(): Promise<string[]> {
  const screens = await listScreens(1);
  const known = [...SEED.screens, ...screens.items];
  for (let page = 2; page <= screens.pages; page++) known.push(...(await listScreens(page)).items);
  return [...new Set(known.map((s) => s.id))].sort();
}
console.log((await options()).join(","));
