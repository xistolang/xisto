// array callbacks that take the array too: xs.filter((x, i, arr) => ...), map, some, every, find, findIndex, forEach
interface D { codigoDedo: number; nome: string }
const ds: D[] = [{ codigoDedo: 2, nome: "a" }, { codigoDedo: 1, nome: "b" }, { codigoDedo: 2, nome: "c" }];
const uniq = ds.map((d) => d).filter((d, i, arr) => arr.findIndex((x) => x.codigoDedo === d.codigoDedo) === i);
console.log(uniq.map((d) => d.nome).join(","));
console.log(ds.map((d, i, a) => `${d.nome}${i}/${a.length}`).join(" "));
console.log(ds.some((d, i, a) => a[a.length - 1 - i].nome === d.nome), ds.every((d, i, a) => a.length === 3));
console.log(ds.find((d, i, a) => i === a.length - 1)?.nome, ds.findIndex((d, i, a) => d.nome === "b"));
ds.forEach((d, i, a) => { if (i === 0) console.log(a.length); });
