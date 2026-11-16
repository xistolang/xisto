// RegExp property escapes (\p{...}, \P{...}) with flag u or v, as Node's (Unicode 17): General_Category values alone
// or as gc= / General_Category=, Script (sc=), Script_Extensions (scx=), binary properties; in and out of classes,
// negated, case-insensitive; and how many code points of the whole range each property matches
console.log(/\p{Lu}+/u.test("abcD"), "Ωmega αβγ".replace(/\p{Script=Greek}/gu, "G"), /\P{L}/u.test("a"));
console.log("Ünïcödé 123 ٣٤ ½ ⅷ".match(/\p{Nd}/gu), "Ünïcödé 123 ٣٤ ½ ⅷ".match(/\p{N}/gu));
console.log("héllo wörld, 你好世界! Привет".match(/\p{L}+/gu));
console.log("你好 hello カタカナ ひらがな".match(/\p{sc=Han}+/gu), "カタカナ ひらがな ー".match(/\p{scx=Hiragana}+/gu));
console.log("a😀b🎉c".replace(/\p{Extended_Pictographic}/gu, "*"), /\p{Emoji_Presentation}/u.test("😀"), /\p{Emoji}/u.test("1"));
console.log("tab\there nbsp em end".split(/\p{White_Space}+/u), /^\p{ASCII}+$/u.test("plain"), /^\p{ASCII}+$/u.test("plaín"));
console.log("Mixed CASE Text".replace(/[\p{Lu}\d]/gu, "_"), "Mixed CASE Text".replace(/[^\p{Ll}\s]/gu, "_"));
console.log("x-y_z.w".match(/[\p{P}]/gu), "1+2=3 $ €".match(/\p{S}/gu), "a b　c".match(/\p{Zs}/gu)?.length);
console.log(/\p{General_Category=Decimal_Number}/u.test("٣"), /\p{gc=Nd}/u.test("x"), /\p{Letter}/u.test("ß"), /\p{Alpha}/u.test("ª"));
console.log(/\p{Lu}/iu.test("a"), /\p{Ll}/iu.test("A"), /[\P{Lu}]/u.test("A"), /\p{Any}/u.test("\u{10FFFF}"));
console.log("ℕ𝔸ℤ".match(/\p{Math}/gu)?.length, /\p{ID_Start}\p{ID_Continue}*/u.exec("  ident_9 x")?.[0], "Ⅻ".match(/\p{Nl}/u)?.[0]);
console.log("abc".match(/\p{L}/gv), /[\p{L}--[a-z]]/v.test("a"), /[\p{L}--[a-z]]/v.test("B"), /[[a-z]&&[aeiou]]/v.test("b"), /[[a-z]&&[aeiou]]/v.test("e"));
console.log("Ünïcode ASCII".replace(/[\p{L}--\p{ASCII}]/gv, "#"), /[^\d[x-z]]/v.test("y"), /x/v.flags, /x/v.unicode, /x/gu.flags);
for (const bad of ["\\p{Foo}", "\\p{sc=Greekk}", "\\p{Script}", "\\p{gc=Latin}", "\\p{lu}"]) {
  try {
    new RegExp(bad, "u");
    console.log("accepted", bad);
  } catch (e) {
    console.log("refused", bad, (e as Error).name);
  }
}
console.log(new RegExp("\\p{L}", "").test("p{L}"), /\p{L}/.test("p{L}"));
// how many code points each property has, over the whole range (surrogates left out)
const parts: string[] = [];
for (let c = 0; c <= 0x10ffff; c++) if (c < 0xd800 || c > 0xdfff) parts.push(String.fromCodePoint(c));
const all = parts.join("");
const props: RegExp[] = [/\p{L}/gu, /\p{Lu}/gu, /\p{Ll}/gu, /\p{Lt}/gu, /\p{Lm}/gu, /\p{Lo}/gu, /\p{LC}/gu, /\p{M}/gu, /\p{Mn}/gu, /\p{N}/gu, /\p{Nd}/gu, /\p{P}/gu, /\p{S}/gu, /\p{Sc}/gu, /\p{Z}/gu, /\p{C}/gu, /\p{Cn}/gu, /\p{Co}/gu, /\p{Cf}/gu, /\p{sc=Latin}/gu, /\p{sc=Greek}/gu, /\p{sc=Cyrillic}/gu, /\p{sc=Han}/gu, /\p{sc=Arabic}/gu, /\p{sc=Common}/gu, /\p{sc=Inherited}/gu, /\p{sc=Unknown}/gu, /\p{scx=Latin}/gu, /\p{scx=Han}/gu, /\p{scx=Arabic}/gu, /\p{Alphabetic}/gu, /\p{White_Space}/gu, /\p{Emoji}/gu, /\p{Extended_Pictographic}/gu, /\p{ASCII}/gu, /\p{Any}/gu, /\p{Assigned}/gu, /\p{Uppercase}/gu, /\p{Lowercase}/gu, /\p{ID_Start}/gu, /\p{XID_Continue}/gu, /\p{Default_Ignorable_Code_Point}/gu, /\P{L}/gu];
for (const re of props) console.log(re.source, all.match(re)?.length ?? 0);
