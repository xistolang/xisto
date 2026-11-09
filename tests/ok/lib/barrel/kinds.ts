// declarations exported by a list at the end (export { a, b };), and a union that opens with '|'
type Kind = | "circle" | "square";

function kindOf(n: number): Kind {
  if (n > 0) return "circle";
  return "square";
}

const KIND_LIMIT: number = 10;

export { kindOf, KIND_LIMIT };
