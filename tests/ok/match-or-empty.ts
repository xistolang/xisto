// s.match(/x/g) ?? []: the matches as a string[], and a Set of them
const message = "ORA-00604 x ORA-20001 ORA-00604";
const a = message.match(/ORA-\d+/g) ?? [];
const codes = [...new Set(message.match(/\b(?:ORA|NJS|DPI)-\d{3,5}\b/g) ?? [])];
console.log(a.length, codes.join(", "));
