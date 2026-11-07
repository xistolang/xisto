// Bun: stream the file and split lines by hand (Bun has no readline-style line iterator on files).
let n = 0;
let tail = "";
const decoder = new TextDecoder();
for await (const chunk of Bun.file(process.argv[2]).stream()) {
  const text = tail + decoder.decode(chunk, { stream: true });
  const parts = text.split("\n");
  tail = parts.pop() ?? "";
  for (const line of parts) if (line.includes("ERROR")) n++;
}
if (tail.includes("ERROR")) n++;
console.log(n);
