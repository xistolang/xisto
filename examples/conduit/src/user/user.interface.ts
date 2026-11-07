export interface UserData {
  id: int;
  username: string;
  email: string;
  bio: string;
  token: string;
  image: string;
}

export interface UserRO {
  user: UserData;
}

export interface LoginData {
  email: string;
  token: string;
  username: string;
  bio: string;
  image: string;
}

export interface LoginRO {
  user: LoginData;
}
