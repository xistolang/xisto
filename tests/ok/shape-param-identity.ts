// An arrow whose parameter is an object type written in place, passed where a function of an interface with the same
// fields goes: one type in TypeScript, so the arrow gets the caller's object (what it writes is seen by the caller).
interface Session {
  callTimeout: number;
  tag?: string;
}

function setUp(fn: (s: Session) => void): string {
  const s: Session = { callTimeout: 0 };
  fn(s);
  return `${s.callTimeout} ${s.tag ?? "-"}`;
}

console.log(setUp((s: { callTimeout: number; tag?: string }) => {
  s.callTimeout = 7;
  s.tag = "seven";
}));
console.log(setUp((s: Session) => {
  s.callTimeout = 8;
}));
