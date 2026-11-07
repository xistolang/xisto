// jsonwebtoken, HS256: sign(payload, secret) adds "iat" like the npm package; verify(token, secret) checks the
// signature and "exp" and returns the payload (an object: decode it with `as YourPayload`).
// Errors carry npm's names and messages: JsonWebTokenError ("jwt malformed", "invalid signature",
// "invalid algorithm"), TokenExpiredError ("jwt expired").
export declare function sign(payload: object, secret: string): string;
export declare function verify(token: string, secret: string): object;
