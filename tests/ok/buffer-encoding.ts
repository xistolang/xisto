// Node's BufferEncoding as a type
const charset = "iso-8859-1";
const enc: BufferEncoding = charset && /utf-?8/.test(charset) ? "utf8" : "latin1";
console.log(Buffer.from([0x63, 0xe7]).toString(enc));
