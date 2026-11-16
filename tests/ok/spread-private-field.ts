class Q { x: number = 1; private _y: number = 2; get y(): number { return this._y; } }
const q = new Q();
const s = { ...q };
console.log(JSON.stringify(s));
const o = Object.assign({}, q);
console.log(JSON.stringify(o));
