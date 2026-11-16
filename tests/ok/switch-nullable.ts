function sw(x: string | null): string {
  switch (x) {
    case "a": return "A:" + x.length;
    case null: return "null";
    default: return "d:" + x;
  }
}
function sw2(x: number | null): string {
  switch (x) {
    case 1: return "one";
    default: return "other " + x;
  }
}
type L = "x" | "y";
function sw3(x: L | null): string {
  switch (x) {
    case "x": return "X";
    case "y": return "Y";
  }
  return "none";
}
console.log(sw("a"), sw(null), sw("zz"), sw2(1), sw2(null), sw2(5), sw3("y"), sw3(null));
