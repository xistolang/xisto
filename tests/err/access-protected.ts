// expect: 'history' is protected in Log
class Log {
  protected history: string[] = [];
}
const l = new Log();
console.log(l.history.length);
