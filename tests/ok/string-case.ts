// toUpperCase / toLowerCase beyond ASCII (Latin, Greek with final sigma, Cyrillic; ß, İ, ŉ change length), and
// Math.min / Math.max as JavaScript has them: any number of arguments, NaN wins, -0 below 0
const ws: string[] = ["ação", "AÇÃO São Paulo", "Straße", "ÿ Ÿ", "İstanbul", "ıi", "ΟΔΥΣΣΕΥΣ", "Σ", "ΑΣ ΑΣ.", "Ελληνικά", "Ωμέγα ΆΈΉΊΌΎΏ", "Москва ЁЖИК ёж", "Łódź ŁÓDŹ", "Žluťoučký kůň", "Ångström Œuvre œ", "µ", "x ÷ ×", "ŉ", "plain ASCII 123", ""];
for (const w of ws) {
  console.log(w.toUpperCase(), "|", w.toLowerCase(), "|", w.toUpperCase().length, w.toLowerCase().length);
}
console.log(Math.min(NaN, 1), Math.max(1, NaN), Math.min(0, -0), Math.max(-0, 0), Math.min(), Math.max(), Math.max(3, 9, 4, 1), Math.min(5, 2, 8));
console.log(1 / Math.min(0, -0), 1 / Math.max(-0, 0), Math.min(1, 2), Math.max(7));
console.log("emoji 😀 ok".toUpperCase());
