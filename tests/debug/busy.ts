let i: int = 0;
let laps: int = 0;
while (laps < 1000000000) {
  i = i + 1;
  if (i === 1000) {
    i = 0;
    laps = laps + 1;
  }
}
console.log(laps);
