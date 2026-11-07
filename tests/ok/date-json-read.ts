// A typed JSON read turns ISO text into a Date (xisto: the declared type is checked and converted; Node would
// keep a string under the Date type)
interface Event {
  name: string;
  at: Date;
  until: Date | null;
}
const ev = JSON.parse('{"name":"launch","at":"2026-11-06T18:00:00.000Z","until":null}') as Event;
console.log(ev.name, ev.at.getUTCHours(), ev.at.toISOString(), ev.until === null);
console.log(JSON.stringify(ev));
