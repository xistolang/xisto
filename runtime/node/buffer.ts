// xisto:buffer (node:buffer): Buffer (a built-in class, also a global), isUtf8, isAscii, atob, btoa, kMaxLength,
// kStringMaxLength, constants and INSPECT_MAX_BYTES. Buffer's methods are compiled to runtime functions
// (src/checker.c, check_bytes_member and check_bytes_static; runtime/xisto_rt.h, xs_buf_*).
// Differences from Node (docs/docs/guides/node-compatibility.md): Blob, File, transcode, isLatin1 and
// resolveObjectURL are missing; INSPECT_MAX_BYTES is a constant (console.log always shows 50 bytes).

declare function buf_is_utf8(input: Uint8Array): boolean;
declare function buf_is_ascii(input: Uint8Array): boolean;
declare function buf_atob(data: string): string;
declare function buf_btoa(data: string): string;

/** The largest size a Buffer may have (Node: 2^53 - 1). */
export const kMaxLength: number = 9007199254740991;
/** The longest string, in UTF-16 units, V8 can hold. */
export const kStringMaxLength: number = 536870888;
export const constants = { MAX_LENGTH: 9007199254740991, MAX_STRING_LENGTH: 536870888 };
/** How many bytes console.log shows of a Buffer. */
export const INSPECT_MAX_BYTES: int = 50;

/** Whether the bytes are well-formed UTF-8. */
export function isUtf8(input: Uint8Array): boolean {
  return buf_is_utf8(input);
}
/** Whether every byte is ASCII (below 0x80). */
export function isAscii(input: Uint8Array): boolean {
  return buf_is_ascii(input);
}
/** Decodes base64 to a string of code points 0..255 (as the global atob). */
export function atob(data: string): string {
  return buf_atob(data);
}
/** Encodes a string of code points 0..255 as base64 (as the global btoa). */
export function btoa(data: string): string {
  return buf_btoa(data);
}
