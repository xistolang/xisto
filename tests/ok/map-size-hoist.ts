// a counting loop that also reads the map's size (the first time each key shows up)
const data: int[] = [];
let seed: int = 7;
for (let i = 0; i < 5000; i++) {
  seed = (seed * 1103 + 12345) % 65536;
  data.push(seed % 97);
}
const counts = new Map<int, int>();
const firstAt: int[] = [];
for (let i = 0; i < data.length; i++) {
  const k = data[i];
  if (counts.has(k)) counts.set(k, counts.get(k)! + 1);
  else counts.set(k, 1);
  if (counts.size > firstAt.length) firstAt.push(i);
}
console.log(counts.size, firstAt.length, firstAt.slice(0, 10), counts.get(data[0]), counts.get(data[4999]));
