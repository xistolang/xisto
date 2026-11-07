// Date: construction (now, ms, ISO and legacy text, parts in local time), getters (local and UTC), setters with
// overflow, ISO/UTC formatting, Date.parse / Date.UTC, invalid dates, arithmetic through getTime().
const epoch = new Date(0);
console.log(epoch.toISOString(), epoch.getTime(), epoch.getUTCFullYear(), epoch.getUTCDay());

const d = new Date("2026-11-06T18:01:02.345Z");
console.log(d.toISOString(), d.toJSON(), d.getTime(), d.valueOf());
console.log(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate(), d.getUTCDay(), d.getUTCHours(), d.getUTCMinutes(), d.getUTCSeconds(), d.getUTCMilliseconds());
console.log(d.toUTCString());

const months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
console.log(months[d.getUTCMonth()]);

// date-only ISO is UTC; date-time without offset is local; offsets are applied
console.log(new Date("2026-11-06").toISOString(), new Date("2026-11").toISOString(), new Date("2026").toISOString());
console.log(new Date("2026-11-06T10:00:00+03:00").toISOString(), new Date("2026-11-06T10:00:00-0530").toISOString());
console.log(Date.parse("2026-11-06T10:00:00Z"), Date.parse("2026-02-30"), Date.parse("not a date"));

// legacy formats, with an explicit zone so the result does not depend on the machine
console.log(new Date("Fri, 06 Nov 2026 10:20:30 GMT").toISOString());
console.log(new Date("Nov 6, 2026 10:20:30 GMT+0200").toISOString());
console.log(new Date("6 November 2026 UTC").toISOString());

// Date.UTC and overflowing fields
console.log(Date.UTC(2026, 10, 6), Date.UTC(2026, 12, 1), Date.UTC(2024, 1, 29, 12, 30), Date.UTC(99, 0));
const u = new Date(Date.UTC(2026, 0, 31));
u.setUTCMonth(1);
console.log(u.toISOString());
u.setUTCDate(0);
console.log(u.toISOString());
u.setUTCHours(25, 61, 61, 1001);
console.log(u.toISOString());
u.setUTCFullYear(2000, 1, 29);
console.log(u.toISOString(), u.setTime(86400000), u.toISOString());

// local construction and getters agree (round trip, whatever the time zone)
const local = new Date(2026, 10, 6, 15, 30, 45, 123);
console.log(local.getFullYear(), local.getMonth(), local.getDate(), local.getHours(), local.getMinutes(), local.getSeconds(), local.getMilliseconds());
local.setDate(local.getDate() + 30);
console.log(local.getFullYear(), local.getMonth(), local.getDate(), local.getDay());
const sameLocal = new Date(local.getFullYear(), local.getMonth(), local.getDate(), local.getHours(), local.getMinutes());
console.log(sameLocal.getHours(), sameLocal.getMinutes());

// differences
const a = new Date("2026-01-01T00:00:00Z");
const b = new Date("2026-11-06T12:00:00Z");
const days = (b.getTime() - a.getTime()) / 86400000;
console.log(days, Math.floor(days));

// invalid dates
const bad = new Date("garbage");
console.log(isNaN(bad.getTime()), bad.toString(), bad.toUTCString());
try {
  bad.toISOString();
} catch (e) {
  console.log((e as Error).name, (e as Error).message);
}

// a copy is independent
const c = new Date(d);
c.setUTCFullYear(2030);
console.log(d.getUTCFullYear(), c.getUTCFullYear());
console.log(d);

// JSON: a Date field is its ISO text
interface Event {
  name: string;
  at: Date;
}
const ev: Event = { name: "launch", at: new Date("2026-11-06T18:00:00Z") };
console.log(JSON.stringify(ev));
console.log(ev);
const list: Date[] = [new Date(0), new Date("2026-01-02T03:04:05Z")];
console.log(JSON.stringify(list), list);
