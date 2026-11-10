const xs = ['b', 'a', 'C', 'ab'];
console.log([...xs].sort(), [...new Set(['z', 'y', 'z'])].sort());
const len: number = 2.5;
console.log(xs.length >= len, xs.length / len, Math.round(len) / 1e1);
