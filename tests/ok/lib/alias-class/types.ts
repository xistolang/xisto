import { Order, Repository } from "./ent.ts";
export type OrderT = Order;
export type OrderRepo = Repository<Order>;
export type Orders = Order[];
export function mkRepo(): OrderRepo { return new Repository<Order>(); }
