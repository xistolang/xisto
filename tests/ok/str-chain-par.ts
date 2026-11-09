// the fused chain (slice + replaceAll of one character + toUpperCase/toLowerCase) over threads, on strings of 4 MB
// and more: the same string as the calls one after another (Node), for every order and an odd length

function text(n: int, seed: int): string {
  const parts: string[] = [];
  const letters = "acgtACGTnN>\n";
  let x = seed;
  let line = "";
  for (let i = 0; i < n; i++) {
    x = (x * 1103 + 12345) % 2147483648;
    line += letters.charAt(x % letters.length);
    if (line.length === 61) {
      parts.push(line);
      line = "";
    }
  }
  parts.push(line);
  return parts.join("");
}

function digest(s: string): string {
  let h = 0;
  for (let i = 0; i < s.length; i += 7) h = (h * 31 + s.charCodeAt(i)) % 1000000007;
  return `${s.length} ${h}`;
}

const big = text(6000017, 7);
console.log(digest(big.slice(3, big.length - 5).replaceAll("\n", "").toUpperCase()));
console.log(digest(big.slice(0, 5000000).replaceAll("\n", "").toLowerCase()));
console.log(digest(big.slice(11).toUpperCase().replaceAll("G", "")));
console.log(digest(big.slice(11).replaceAll("g", "").toUpperCase()));
console.log(digest(big.slice(1, 4194305).replaceAll(">", "")));
console.log(digest(big.slice(2).toLowerCase()));
