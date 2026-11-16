// m.groups["name"] and m.groups[k]: a named group read by index, as m.groups.name
const m = /(?<y>\d{4})-(?<mo>\d\d)/.exec("2024-05")!;
console.log(m.groups!["y"], m.groups!.mo, m.groups!["mo"]);
const key = "mo";
console.log(m.groups![key]);
for (const k of ["y", "mo"]) console.log(k, m.groups![k]);
const g = m.groups!;
console.log(g["y"] + "/" + g["mo"]);
