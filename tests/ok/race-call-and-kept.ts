// XB-231: Promise.race of a call and a promise kept in a variable (a timeout whose timer is cleared afterwards): the
// race is on the two promises, so no task stays parked on the loser; the program ends at once with nothing left alive
async function quick(): Promise<string> {
  await new Promise<void>((r) => {
    setTimeout(r, 5);
  });
  return "quick";
}

async function slowish(): Promise<string> {
  await new Promise<void>((r) => {
    setTimeout(r, 80);
  });
  console.log("slowish done (the loser runs to its end)");
  return "slowish";
}

async function failing(): Promise<string> {
  await new Promise<void>((r) => {
    setTimeout(r, 5);
  });
  throw new Error("boom");
}

async function main(): Promise<void> {
  const t0 = Date.now();
  let timer = 0;
  const wait = new Promise<string>((resolve) => {
    timer = setTimeout(() => resolve("timed out"), 20000);
  });
  console.log(await Promise.race([quick(), wait]));
  clearTimeout(timer);

  let timer2 = 0;
  const wait2 = new Promise<string>((resolve) => {
    timer2 = setTimeout(() => resolve("timed out"), 10);
  });
  console.log(await Promise.race([wait2, slowish()]));
  clearTimeout(timer2);

  let timer3 = 0;
  const wait3 = new Promise<string>((resolve) => {
    timer3 = setTimeout(() => resolve("timed out"), 20000);
  });
  try {
    console.log(await Promise.race([failing(), wait3]));
  } catch (e) {
    console.log("caught", (e as Error).message);
  }
  clearTimeout(timer3);
  console.log("quick exit", Date.now() - t0 < 5000);
}
main();
