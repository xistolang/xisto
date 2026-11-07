// Date in local time: construction, DST edges (depends on TZ; Node runs in the same one), en-US locale strings
const gap = new Date(2026, 2, 8, 2, 30);
const overlap = new Date(2026, 10, 1, 1, 30);
console.log(gap.toISOString(), gap.getHours(), overlap.toISOString(), overlap.getHours());
console.log(new Date("2026-03-08T02:30:00").toISOString(), new Date("2026/11/06 10:20").toISOString(), new Date("11/06/2026").toISOString());
console.log(new Date(2026, 0, 1).getTimezoneOffset(), new Date(2026, 6, 1).getTimezoneOffset());
console.log(new Date(2026, 10, 6, 15, 4, 5).toLocaleString(), new Date(2026, 10, 6, 0, 4, 5).toLocaleTimeString(), new Date(2026, 10, 6).toLocaleDateString());
console.log(new Date(2026, 10, 6).toDateString());
