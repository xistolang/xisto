// What the identity provider puts in a token, and the user the strategies make of it.
export class TokenPayload {
  sub: string;
  name: string;
  roles: string[];
  iss: string;
  exp: number;
}

export class AuthUser {
  sub: string;
  name: string;
  roles: string[];
  via: string;
  constructor(sub: string, name: string, roles: string[], via: string) {
    this.sub = sub;
    this.name = name;
    this.roles = roles;
    this.via = via;
  }
}
