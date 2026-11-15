// XB-193: decodeURIComponent / decodeURI of a malformed escape or an invalid UTF-8 sequence throws URIError: URI malformed
const inputs = ["%C3%A9", "caf%C3%A9%20x", "%F0%9F%98%80", "%E2%82%AC", "%C3", "%E0%80%80", "%C0%AF", "%ED%A0%80",
  "%F4%90%80%80", "%80", "%FF", "%C3%28", "%E2%82", "%E2%82%2", "%", "%4", "%G1", "a%2Fb%3F", "%F0%9F%98", "plain é"];
for (const s of inputs) {
  try {
    console.log(JSON.stringify(s), "->", JSON.stringify(decodeURIComponent(s)), JSON.stringify(decodeURI(s)));
  } catch (e) {
    console.log(JSON.stringify(s), e instanceof URIError, (e as Error).name, (e as Error).message, String(e));
  }
}
