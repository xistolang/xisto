// a function-valued field called on a call result (mk().end()): the call runs once
interface Span { name: string; end: () => void }
let made = 0;
function mk(): Span { made++; return { name: "s" + made, end: (): void => { console.log("end", made); } }; }
mk().end();
const holder = { get: (): Span => mk() };
holder.get().end();
console.log(made);
