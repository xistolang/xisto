// build: --tsc-checked
// 'k' in x narrows an unknown to an object with k (TypeScript 4.9): x.k reads by name
function parse(body: unknown): string {
  if (typeof body !== "object" || body === null || !("link" in body)) return "none";
  const link = body.link;
  if (link === null) return "null link";
  return JSON.stringify(link);
}
console.log(parse(JSON.parse('{"link":{"op":"A"}}')), parse(JSON.parse('{"link":null}')), parse(3));
