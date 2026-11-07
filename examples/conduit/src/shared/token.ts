import { sign, verify } from "jsonwebtoken";
import { SECRET } from "../config";

export interface TokenPayload {
  id: int;
  username: string;
  email: string;
  exp: int;
}

export function generateJWT(id: int, username: string, email: string): string {
  const exp: int = Math.floor(Date.now() / 1000) + 60 * 24 * 60 * 60;
  const payload: TokenPayload = { id: id, username: username, email: email, exp: exp };
  return sign(payload, SECRET);
}

// "Token <jwt>" (RealWorld) or "Bearer <jwt>": the payload, or null when absent or invalid
export function readToken(authorization: string): TokenPayload | null {
  const parts: string[] = authorization.split(" ");
  if (parts.length < 2 || parts[1].length === 0) {
    return null;
  }
  try {
    const decoded = verify(parts[1], SECRET) as TokenPayload;
    return decoded;
  } catch (e) {
    return null;
  }
}
