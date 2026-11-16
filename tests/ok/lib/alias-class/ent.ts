export class Order { id = 0; items: string[] = []; total(): number { return this.items.length; } }
export class Repository<T> { rows: T[] = []; add(x: T): void { this.rows.push(x); } }
