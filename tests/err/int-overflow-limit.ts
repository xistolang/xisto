// expect: runtime error: integer overflow
// the limit n + 1 is computed once, after the start (emit.c, for_hoisted_cond): its overflow is still an error
let big: int = 9007199254740991;
let ran = 0;
for (let i = 0; i < big + 1; i++) ran++;
console.log(ran);
