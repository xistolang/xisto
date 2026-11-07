// Getters, setters and private state in a hot loop: 20M accessor calls.
class Vec {
  private _x: number = 0;
  private _y: number = 0;
  get x(): number {
    return this._x;
  }
  set x(v: number) {
    this._x = v;
  }
  get y(): number {
    return this._y;
  }
  set y(v: number) {
    this._y = v;
  }
  get length(): number {
    return Math.sqrt(this._x * this._x + this._y * this._y);
  }
}
const v = new Vec();
let acc = 0;
for (let i: int = 0; i < 5000000; i++) {
  v.x = v.x + 1;
  v.y = v.y + 0.5;
  acc += v.length;
}
console.log(acc.toFixed(1));
