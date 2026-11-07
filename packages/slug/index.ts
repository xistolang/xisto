// slug(text, { lower }): letters and digits kept, Latin accents transliterated, runs of anything else become
// one "-" (or the replacement), none at the ends. The contract of npm's slug for Latin text.
interface SlugOptions {
  lower?: boolean;
  replacement?: string;
}

const ALNUM = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const FROM = "àáâãäåāçćčèéêëēěìíîïīñńňòóôõöøōùúûüūýÿžźżšśłđÀÁÂÃÄÅĀÇĆČÈÉÊËĒĚÌÍÎÏĪÑŃŇÒÓÔÕÖØŌÙÚÛÜŪÝŸŽŹŻŠŚŁĐ";
const TO = "aaaaaaaccceeeeeeiiiiinnnooooooouuuuuyyzzzssldAAAAAAACCCEEEEEEIIIIINNNOOOOOOOUUUUUYYZZZSSLD";

export default function slug(text: string, options: object): string {
  const opts = options as SlugOptions;
  const sep = opts.replacement || "-";
  let out = "";
  let pending = false;
  for (let i: int = 0; i < text.length; i++) {
    let c = text.charAt(i);
    const k = FROM.indexOf(c, 0);
    if (k >= 0) {
      c = TO.charAt(k);
    }
    if (ALNUM.includes(c)) {
      if (pending && out.length > 0) {
        out = out + sep;
      }
      pending = false;
      out = out + c;
    } else {
      pending = true;
    }
  }
  if (opts.lower === true) {
    return out.toLowerCase();
  }
  return out;
}
