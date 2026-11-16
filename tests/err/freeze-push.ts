// expect: cannot write to 'xs': it was frozen with Object.freeze (line 3)
const xs: number[] = [1];
Object.freeze(xs);
xs.push(2);
