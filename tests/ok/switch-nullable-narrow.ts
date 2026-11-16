function code(x: number | null): string {
  switch (x) {
    case 1:
      return "one " + (x + 1);
    case null:
      return "nothing";
    case 2:
    case 3:
      return "two or three " + x.toFixed(1);
    default:
      return "other " + x;
  }
}
console.log(code(1), code(null), code(2), code(3), code(7.5));

function count(n: int | null): string {
  let out = "";
  switch (n) {
    case null:
      out += "null;";
    case 0:
      out += "zero-or-null;";
      break;
    case 5:
      out += "five " + String(n * 2) + ";";
      break;
  }
  return out;
}
console.log(count(null), count(0), count(5), count(9));

type Mode = "fast" | "slow";
class Runner {
  mode: Mode | null = null;
  label: string | null = null;
  describe(): string {
    switch (this.mode) {
      case "fast": return "F" + this.mode.length;
      case "slow": return "S";
      case null: return "unset";
    }
    return "?";
  }
  tag(): string {
    switch (this.label) {
      case "x": return "label x of " + this.label.length;
      default: return "label " + (this.label ?? "none");
    }
  }
}
const r = new Runner();
console.log(r.describe(), r.tag());
r.mode = "fast";
r.label = "x";
console.log(r.describe(), r.tag());
r.mode = "slow";
r.label = "y";
console.log(r.describe(), r.tag());

function lit(s: Mode | null): string {
  switch (s) {
    case "fast":
      return s.toUpperCase();
    default:
      return "default " + s;
  }
}
console.log(lit("fast"), lit("slow"), lit(null));

function words(s: string | null): string[] {
  const out: string[] = [];
  switch (s) {
    case "a":
      out.push("a:" + s.length);
    case "b":
      out.push("b:" + s.length);
      break;
    case null:
      out.push("null");
      break;
  }
  return out;
}
console.log(words("a"), words("b"), words(null), words("c"));
