// Count ERROR lines with a stream: memory stays at one 64 KB buffer.
const path: string = process.argv[2];
const n: int = lines(path).filter((l) => l.includes("ERROR")).count();
console.log(n);
