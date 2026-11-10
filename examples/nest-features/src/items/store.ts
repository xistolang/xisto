import { Injectable } from "@nestjs/common";
import { Item } from "./item.dto";

@Injectable()
export class MemoryStore {
  readonly items: Item[] = [];
  next: number = 1;
}
