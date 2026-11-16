// expect: cannot write to 'CFG': it was frozen with Object.freeze (line 2)
const CFG = Object.freeze({ a: 1 });
CFG.a = 2;
