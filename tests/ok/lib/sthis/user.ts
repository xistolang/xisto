import { Model } from "./model.ts";
export class User extends Model {
  static table = "users";
  static kind(): string { return "user"; }
  name(): string { return "User"; }
}
